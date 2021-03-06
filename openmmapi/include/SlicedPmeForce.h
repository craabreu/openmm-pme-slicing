#ifndef OPENMM_SLICEDPMEFORCE_H_
#define OPENMM_SLICEDPMEFORCE_H_

/* -------------------------------------------------------------------------- *
 *                                   OpenMM                                   *
 * -------------------------------------------------------------------------- *
 * This is part of the OpenMM molecular simulation toolkit originating from   *
 * Simbios, the NIH National Center for Physics-Based Simulation of           *
 * Biological Structures at Stanford, funded under the NIH Roadmap for        *
 * Medical Research, grant U54 GM072970. See https://simtk.org.               *
 *                                                                            *
 * Portions copyright (c) 2008-2022 Stanford University and the Authors.      *
 * Authors: Peter Eastman                                                     *
 * Contributors:                                                              *
 *                                                                            *
 * Permission is hereby granted, free of charge, to any person obtaining a    *
 * copy of this software and associated documentation files (the "Software"), *
 * to deal in the Software without restriction, including without limitation  *
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,   *
 * and/or sell copies of the Software, and to permit persons to whom the      *
 * Software is furnished to do so, subject to the following conditions:       *
 *                                                                            *
 * The above copyright notice and this permission notice shall be included in *
 * all copies or substantial portions of the Software.                        *
 *                                                                            *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR *
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   *
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    *
 * THE AUTHORS, CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,    *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR      *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE  *
 * USE OR OTHER DEALINGS IN THE SOFTWARE.                                     *
 * -------------------------------------------------------------------------- */

#include "openmm/Context.h"
#include "openmm/Force.h"
#include "openmm/NonbondedForce.h"
#include <map>
#include <set>
#include <utility>
#include <vector>
#include "internal/windowsExportPmeSlicing.h"

#define DEFALT_USE_CUDA_FFT false

using namespace OpenMM;

namespace PmeSlicing {

/**
 * This class implements a Coulomb force to represent electrostatic interactions between particles
 * under periodic boundary conditions. The computation is done using the smooth Particle Mesh Ewald
 * (PME) method.
 *
 * The total Coulomb potential can be divided into slices depending on which pairs of particles are
 * involved. After distributing all particles among disjoint subsets, each slice is distinguished
 * by two indices I and J. Slice[I,J] is the sum of interactions of all particles in subset I with
 * all particles in subset J.
 *
 * To use this class, create a SlicedPmeForce object, then call addParticle() once for each
 * particle in the System to define its electric charge and its subset. The number of particles
 * for which you define these parameters must be exactly equal to the number of particles in the
 * System, or else an exception will be thrown when you try to create a Context. After a particle
 * has been added, you can modify its electric charge by calling setParticleCharge() or its subset
 * by calling setParticleSubset(). This will have no effect on Contexts that already exist unless
 * you call updateParametersInContext().
 *
 * SlicedPmeForce also lets you specify "exceptions", particular pairs of particles whose
 * interactions should be computed based on different parameters than those defined for the
 * individual particles. This can be used to completely exclude certain interactions from the
 * force calculation, or to alter how they interact with each other.
 *
 * Many molecular force fields omit Coulomb interactions between particles separated by one
 * or two bonds, while using modified parameters for those separated by three bonds (known as
 * "1-4 interactions"). This class provides a convenience method for this case called
 * createExceptionsFromBonds().  You pass to it a list of bonds and the scale factors to use
 * for 1-4 interactions.  It identifies all pairs of particles which are separated by 1, 2, or
 * 3 bonds, then automatically creates exceptions for them.
 *
 * In some applications, it is useful to be able to inexpensively change the charges of small
 * groups of particles. Usually this is done to interpolate between two sets of parameters. For
 * example, a titratable group might have two states it can exist in, each described by a
 * different set of parameters for the atoms that make up the group. You might then want to
 * smoothly interpolate between the two states. This is done by first calling addGlobalParameter()
 * to define a Context parameter, then addParticleParameterOffset() to create a "parameter offset"
 * that depends on the Context parameter. Each offset defines the following:
 *
 * <ul>
 * <li>A Context parameter used to interpolate between the states.</li>
 * <li>A single particle whose parameters are influenced by the Context parameter.</li>
 * <li>A scale factor (chargeScale) that specifies how the Context parameter affects the
 * particle.</li>
 * </ul>
 *
 * The "effective" charge of a particle (that used to compute forces) is given by
 *
 * \verbatim embed:rst:leading-asterisk
 * .. code-block:: cpp
 *
 *    charge = baseCharge + param*chargeScale
 *
 * \endverbatim
 *
 * where the "base" values are the ones specified by addParticle() and "param" is the current value
 * of the Context parameter. A single Context parameter can apply offsets to multiple particles,
 * and multiple parameters can be used to apply offsets to the same particle.  Parameters can also
 * be used to modify exceptions in exactly the same way by calling addExceptionParameterOffset().
 */

class OPENMM_EXPORT_PMESLICING SlicedPmeForce : public Force {
public:
    /**
     * Create a SlicedPmeForce.
     * 
     * @param numSubsets the number of particle subsets.
     */
    SlicedPmeForce(int numSubsets=1);
    /**
     * Create a SlicedPmeForce whose properties are imported from an existing NonbondedForce.
     * 
     * @param nonbondedForce the NonbondedForce whose properties will be imported.
     * @param numSubsets the number of particle subsets.
     */
    SlicedPmeForce(const NonbondedForce&, int numSubsets=1);
    /**
     * Get the specified number of particle subsets.
     */
    int getNumSubsets() const {
        return numSubsets;
    }
    /**
     * Get the number of particles for which force field parameters have been defined.
     */
    int getNumParticles() const {
        return particles.size();
    }
    /**
     * Get the number of special interactions that should be calculated differently from other
     * interactions.
     */
    int getNumExceptions() const {
        return exceptions.size();
    }
    /**
     * Get the number of global parameters that have been added.
     */
    int getNumGlobalParameters() const {
        return globalParameters.size();
    }
    /**
     * Get the number of particles parameter offsets that have been added.
     */
    int getNumParticleParameterOffsets() const {
        return particleOffsets.size();
    }
    /**
     * Get the number of exception parameter offsets that have been added.
     */
    int getNumExceptionParameterOffsets() const {
        return exceptionOffsets.size();
    }
    /**
     * Get the cutoff distance (in nm) being used for nonbonded interactions.
     *
     * @return the cutoff distance, measured in nm
     */
    double getCutoffDistance() const;
    /**
     * Set the cutoff distance (in nm) being used for nonbonded interactions.
     *
     * @param distance    the cutoff distance, measured in nm
     */
    void setCutoffDistance(double distance);
    /**
     * Get the error tolerance for Ewald summation.  This corresponds to the fractional error in
     * the forces which is acceptable.  This value is used to select the reciprocal space cutoff
     * and separation parameter so that the average error level will be less than the tolerance.
     * There is not a rigorous guarantee that all forces on all atoms will be less than the
     * tolerance, however.
     *
     * For PME calculations, if setPMEParameters() is used to set alpha to something other than 0,
     * this value is ignored.
     */
    double getEwaldErrorTolerance() const;
    /**
     * Set the error tolerance for Ewald summation.  This corresponds to the fractional error in
     * the forces which is acceptable.  This value is used to select the reciprocal space cutoff
     * and separation parameter so that the average error level will be less than the tolerance.
     * There is not a rigorous guarantee that all forces on all atoms will be less than the
     * tolerance, however.
     *
     * For PME calculations, if setPMEParameters() is used to set alpha to something other than 0,
     * this value is ignored.
     */
    void setEwaldErrorTolerance(double tol);
    /**
     * Get the parameters to use for PME calculations. If alpha is 0 (the default),
     * these parameters are ignored and instead their values are chosen based on the Ewald error
     * tolerance.
     *
     * @param[out] alpha   the separation parameter
     * @param[out] nx      the number of grid points along the X axis
     * @param[out] ny      the number of grid points along the Y axis
     * @param[out] nz      the number of grid points along the Z axis
     */
    void getPMEParameters(double& alpha, int& nx, int& ny, int& nz) const;
    /**
     * Set the parameters to use for PME calculations.  If alpha is 0 (the default), these
     * parameters are ignored and instead their values are chosen based on the Ewald error
     * tolerance.
     *
     * @param alpha   the separation parameter
     * @param nx      the number of grid points along the X axis
     * @param ny      the number of grid points along the Y axis
     * @param nz      the number of grid points along the Z axis
     */
    void setPMEParameters(double alpha, int nx, int ny, int nz);
    /**
     * Get the parameters being used for PME in a particular Context.  Because some platforms have
     * restrictions on the allowed grid sizes, the values that are actually used may be slightly
     * different from those specified with setPMEParameters(), or the standard values calculated
     * based on the Ewald error tolerance. See the manual for details.
     *
     * @param context      the Context for which to get the parameters
     * @param[out] alpha   the separation parameter
     * @param[out] nx      the number of grid points along the X axis
     * @param[out] ny      the number of grid points along the Y axis
     * @param[out] nz      the number of grid points along the Z axis
     */
    void getPMEParametersInContext(const Context& context, double& alpha, int& nx, int& ny, int& nz) const;
    /**
     * Add the charges and (optionally) the subset for a particle.  This should be called once
     * for each particle in the System.  When it is called for the i'th time, it specifies the
     * charge for the i'th particle.
     *
     * @param charge    the charge of the particle, measured in units of the proton charge
     * @param subset    the subset to which this particle belongs (default=0)
     * @return the index of the particle that was added
     */
    int addParticle(double charge, int subset=0);
    /**
     * Get the subset to which a particle belongs.
     *
     * @param index          the index of the particle for which to get the subset
     */
    int getParticleSubset(int index) const;
    /**
     * Set the subset for a particle.
     *
     * @param index     the index of the particle for which to set the subset
     * @param subset    the subset to which this particle belongs
     */
    void setParticleSubset(int index, int subset);
    /**
     * Get the charge of a particle.
     *
     * @param index          the index of the particle for which to get parameters
     * @param[out] charge    the charge of the particle, measured in units of the proton charge
     */
    double getParticleCharge(int index) const;
    /**
     * Set the charge for a particle.
     *
     * @param index     the index of the particle for which to set parameters
     * @param charge    the charge of the particle, measured in units of the proton charge
     */
    void setParticleCharge(int index, double charge);
    /**
     * Add an interaction to the list of exceptions that should be calculated differently from
     * other interactions. If chargeProd is equal to 0, this will cause the interaction to be
     * completely omitted from force and energy calculations.
     * 
     * Cutoffs are never applied to exceptions. That is because they are primarily used for 1-4
     * interactions, which are really a type of bonded interaction and are parametrized together
     * with the other bonded interactions.
     *
     * In many cases, you can use createExceptionsFromBonds() rather than adding each exception
     * explicitly.
     *
     * @param particle1  the index of the first particle involved in the interaction
     * @param particle2  the index of the second particle involved in the interaction
     * @param chargeProd the scaled product of the atomic charges (i.e. the strength of the Coulomb
     *                   interaction), measured in units of the proton charge squared
     * @param replace    determines the behavior if there is already an exception for the same two
     *                   particles. If true, the existing one is replaced. If false, an exception
     *                   is thrown.
     * @return the index of the exception that was added
     */
    int addException(int particle1, int particle2, double chargeProd, bool replace = false);
    /**
     * Get the particle indices and charge product for an interaction that should be calculated
     * differently from others.
     *
     * @param index           the index of the interaction for which to get parameters
     * @param[out] particle1  the index of the first particle involved in the interaction
     * @param[out] particle2  the index of the second particle involved in the interaction
     * @param[out] chargeProd the scaled product of the atomic charges (i.e. the strength of the
     *                        Coulomb interaction), measured in units of the proton charge squared
     */
    void getExceptionParameters(int index, int& particle1, int& particle2, double& chargeProd) const;
    /**
     * Set the particle indices and charge product for an interaction that should be calculated
     * differently from others. If chargeProd is equal to 0, this will cause the interaction to be
     * completely omitted from force and energy calculations.
     * 
     * Cutoffs are never applied to exceptions. That is because they are primarily used for 1-4
     * interactions, which are really a type of bonded interaction and are parametrized together
     * with the other bonded interactions.
     *
     * @param index      the index of the interaction for which to get parameters
     * @param particle1  the index of the first particle involved in the interaction
     * @param particle2  the index of the second particle involved in the interaction
     * @param chargeProd the scaled product of the atomic charges (i.e. the strength of the Coulomb
     *                   interaction), measured in units of the proton charge squared
     */
    void setExceptionParameters(int index, int particle1, int particle2, double chargeProd);
    /**
     * Identify exceptions based on the molecular topology.  Particles which are separated by one
     * or two bonds are set to not interact at all, while pairs of particles separated by three
     * bonds (known as "1-4 interactions") have their Coulomb and Lennard-Jones interactions
     * reduced by a fixed factor.
     *
     * @param bonds           the set of bonds based on which to construct exceptions. Each element
     *                        specifies the indices of two particles that are bonded to each other.
     * @param coulomb14Scale  pairs of particles separated by three bonds will have the strength of
     *                        their Coulomb interaction multiplied by this factor
     * @param lj14Scale       pairs of particles separated by three bonds will have the strength of
     *                        their Lennard-Jones interaction multiplied by this factor
     */
    void createExceptionsFromBonds(const std::vector<std::pair<int, int> >& bonds, double coulomb14Scale, double lj14Scale);
    /**
     * Add a new global parameter that parameter offsets may depend on.  The default value provided
     * to this method is the initial value of the parameter in newly created Contexts.  You can
     * change the value at any time by calling setParameter() on the Context.
     * 
     * @param name             the name of the parameter
     * @param defaultValue     the default value of the parameter
     * @return the index of the parameter that was added
     */
    int addGlobalParameter(const std::string& name, double defaultValue);
    /**
     * Get the name of a global parameter.
     *
     * @param index     the index of the parameter for which to get the name
     * @return the parameter name
     */
    const std::string& getGlobalParameterName(int index) const;
    /**
     * Set the name of a global parameter.
     *
     * @param index          the index of the parameter for which to set the name
     * @param name           the name of the parameter
     */
    void setGlobalParameterName(int index, const std::string& name);
    /**
     * Get the default value of a global parameter.
     *
     * @param index     the index of the parameter for which to get the default value
     * @return the parameter default value
     */
    double getGlobalParameterDefaultValue(int index) const;
    /**
     * Set the default value of a global parameter.
     *
     * @param index          the index of the parameter for which to set the default value
     * @param defaultValue   the default value of the parameter
     */
    void setGlobalParameterDefaultValue(int index, double defaultValue);
    /**
     * Add an offset to the charge of a particular particle, based on a global parameter.
     * 
     * @param parameter       the name of the global parameter. It must have already been added
     *                        with addGlobalParameter(). Its value can be modified at any time by
     *                        calling Context::setParameter().
     * @param particleIndex   the index of the particle whose parameters are affected
     * @param chargeScale     this value multiplied by the parameter value is added to the
     *                        particle's charge
     * @return the index of the offset that was added
     */
    int addParticleParameterOffset(const std::string& parameter, int particleIndex, double chargeScale);
    /**
     * Get the offset added to the per-particle parameters of a particular particle, based on a
     * global parameter.
     * 
     * @param index           the index of the offset to query, as returned by
     *                        addParticleParameterOffset()
     * @param parameter       the name of the global parameter
     * @param particleIndex   the index of the particle whose parameters are affected
     * @param chargeScale     this value multiplied by the parameter value is added to the
     *                        particle's charge
     */
    void getParticleParameterOffset(int index, std::string& parameter, int& particleIndex, double& chargeScale) const;
    /**
     * Set the offset added to the per-particle parameters of a particular particle, based on a
     * global parameter.
     * 
     * @param index           the index of the offset to modify, as returned by
     *                        addParticleParameterOffset()
     * @param parameter       the name of the global parameter. It must have already been added
     *                        with addGlobalParameter(). Its value can be modified at any time by
     *                        calling Context::setParameter().
     * @param particleIndex   the index of the particle whose parameters are affected
     * @param chargeScale     this value multiplied by the parameter value is added to the
     *                        particle's charge
     */
    void setParticleParameterOffset(int index, const std::string& parameter, int particleIndex, double chargeScale);
    /**
     * Add an offset to the parameters of a particular exception, based on a global parameter.
     * 
     * @param parameter       the name of the global parameter.  It must have already been added
     *                        with addGlobalParameter(). Its value can be modified at any time by
     *                        calling Context::setParameter().
     * @param exceptionIndex  the index of the exception whose parameters are affected
     * @param chargeProdScale this value multiplied by the parameter value is added to the
     *                        exception's charge product
     * @return the index of the offset that was added
     */
    int addExceptionParameterOffset(const std::string& parameter, int exceptionIndex, double chargeProdScale);
    /**
     * Get the offset added to the parameters of a particular exception, based on a global
     * parameter.
     * 
     * @param index           the index of the offset to query, as returned by
     *                        addExceptionParameterOffset()
     * @param parameter       the name of the global parameter
     * @param exceptionIndex  the index of the exception whose parameters are affected
     * @param chargeProdScale this value multiplied by the parameter value is added to the
     *                        exception's charge product
     */
    void getExceptionParameterOffset(int index, std::string& parameter, int& exceptionIndex, double& chargeProdScale) const;
    /**
     * Set the offset added to the parameters of a particular exception, based on a global
     * parameter.
     * 
     * @param index           the index of the offset to modify, as returned by
     *                        addExceptionParameterOffset()
     * @param parameter       the name of the global parameter.  It must have already been added
     *                        with addGlobalParameter(). Its value can be modified at any time by
     *                        calling Context::setParameter().
     * @param exceptionIndex  the index of the exception whose parameters are affected
     * @param chargeProdScale this value multiplied by the parameter value is added to the
     *                        exception's charge product
     */
    void setExceptionParameterOffset(int index, const std::string& parameter, int exceptionIndex, double chargeProdScale);
    /**
     * Get the force group that reciprocal space interactions for Ewald or PME are included in.  This allows multiple
     * time step integrators to evaluate direct and reciprocal space interactions at different intervals: getForceGroup()
     * specifies the group for direct space, and getReciprocalSpaceForceGroup() specifies the group for reciprocal space.
     * If this is -1 (the default value), the same force group is used for reciprocal space as for direct space.
     */
    int getReciprocalSpaceForceGroup() const;
    /**
     * Set the force group that reciprocal space interactions for Ewald or PME are included in.  This allows multiple
     * time step integrators to evaluate direct and reciprocal space interactions at different intervals: setForceGroup()
     * specifies the group for direct space, and setReciprocalSpaceForceGroup() specifies the group for reciprocal space.
     * If this is -1 (the default value), the same force group is used for reciprocal space as for direct space.
     *
     * @param group    the group index.  Legal values are between 0 and 31 (inclusive), or -1 to use the same force group
     *                 that is specified for direct space.
     */
    void setReciprocalSpaceForceGroup(int group);
    /**
     * Get whether to include direct space interactions when calculating forces and energies.  This is useful if you want
     * to completely replace the direct space calculation, typically with a CustomSlicedPmeForce that computes it in a
     * nonstandard way, while still using this object for the reciprocal space calculation.
     */
    bool getIncludeDirectSpace() const;
    /**
     * Set whether to include direct space interactions when calculating forces and energies.  This is useful if you want
     * to completely replace the direct space calculation, typically with a CustomSlicedPmeForce that computes it in a
     * nonstandard way, while still using this object for the reciprocal space calculation.
     */
    void setIncludeDirectSpace(bool include);
    /**
     * Update the particle and exception parameters in a Context to match those stored in this Force object.  This method
     * provides an efficient method to update certain parameters in an existing Context without needing to reinitialize it.
     * Simply call setParticleCharge() and setExceptionParameters() to modify this object's parameters, then call
     * updateParametersInContext() to copy them over to the Context.
     *
     * This method has several limitations.  The only information it updates is the parameters of particles and exceptions.
     * All other aspects of the Force (the nonbonded method, the cutoff distance, etc.) are unaffected and can only be
     * changed by reinitializing the Context.  Furthermore, only the chargeProd, sigma, and epsilon values of an exception
     * can be changed; the pair of particles involved in the exception cannot change.  Finally, this method cannot be used
     * to add new particles or exceptions, only to change the parameters of existing ones.
     */
    void updateParametersInContext(Context& context);
    /**
     * Get whether periodic boundary conditions should be applied to exceptions.  Usually this is not
     * appropriate, because exceptions are normally used to represent bonded interactions (1-2, 1-3, and
     * 1-4 pairs), but there are situations when it does make sense.  For pmeslicing, you may want to simulate
     * an infinite chain where one end of a molecule is bonded to the opposite end of the next periodic
     * copy.
     * 
     * Regardless of this value, periodic boundary conditions are only applied to exceptions if they also
     * are applied to other interactions.  If the nonbonded method is NoCutoff or CutoffNonPeriodic, this
     * value is ignored.  Also note that cutoffs are never applied to exceptions, again because they are
     * normally used to represent bonded interactions.
     */
    bool getExceptionsUsePeriodicBoundaryConditions() const;
    /**
     * Set whether periodic boundary conditions should be applied to exceptions. Usually this is
     * not appropriate, because exceptions are normally used to represent bonded interactions
     * (1-2, 1-3, and 1-4 pairs), but there are situations when it does make sense.  For example,
     * you may want to simulate an infinite chain where one end of a molecule is bonded to the
     * opposite end of the next periodic copy.
     * 
     * Regardless of this value, periodic boundary conditions are only applied to exceptions if
     * they also get applied to other interactions.  If the nonbonded method is NoCutoff or
     * CutoffNonPeriodic, this value is ignored.  Also note that cutoffs are never applied to
     * exceptions, again because they are normally used to represent bonded interactions.
     */
    void setExceptionsUsePeriodicBoundaryConditions(bool periodic);
 	/**
     * Get the force group of a particular nonbonded slice. If this is -1 (the default value), the
     * actual force group is the one obtained via getForceGroup.
     * 
     * @param subset1  the index of a particle subset.  Legal values are between 0 and numSubsets.
     * @param subset2  the index of a particle subset.  Legal values are between 0 and numSubsets.
     */
    int getSliceForceGroup(int subset1, int subset2) const;
 	/**
     * Set the force group of a particular nonbonded slice, concerning the interactions between
     * particles of a subset with those of another (or the same) subset.
     * 
     * @param subset1  the index of a particle subset.  Legal values are between 0 and numSubsets.
     * @param subset2  the index of a particle subset.  Legal values are between 0 and numSubsets.
     * @param group    the group index.  Legal values are between 0 and 31 (inclusive), or -1 to
     *                 use the same force group that is specified via setForceGroup.
     */
    void setSliceForceGroup(int subset1, int subset2, int group);
 	/**
     * Get whether CUDA Toolkit's cuFFT library is used to compute fast Fourier transform when
     * executing in the CUDA platform.
     */
    bool getUseCudaFFT() const {
        return useCudaFFT;
    };
 	/**
     * Set whether to use CUDA Toolkit's cuFFT library to compute fast Fourier transform when
     * executing in the CUDA platform. The default value is 'DEFALT_USE_CUDA_FFT'. This choice
     * has no effect when using platforms other than CUDA or when the CUDA Toolkit version is
     * 7.0 or older.
     */
    void setUseCuFFT(bool use) {
        useCudaFFT = use;
    };
protected:
    ForceImpl* createImpl() const;
    bool usesPeriodicBoundaryConditions() const {return true;}
private:
    class ParticleInfo;
    class ExceptionInfo;
    class GlobalParameterInfo;
    class ParticleOffsetInfo;
    class ExceptionOffsetInfo;
    int numSubsets;
    double cutoffDistance, ewaldErrorTol, alpha, dalpha;
    bool exceptionsUsePeriodic, includeDirectSpace;
    int recipForceGroup, nx, ny, nz, dnx, dny, dnz;
    bool useCudaFFT;
    void addExclusionsToSet(const std::vector<std::set<int> >& bonded12, std::set<int>& exclusions, int baseParticle, int fromParticle, int currentLevel) const;
    int getGlobalParameterIndex(const std::string& parameter) const;
    std::vector<ParticleInfo> particles;
    std::vector<ExceptionInfo> exceptions;
    std::vector<GlobalParameterInfo> globalParameters;
    std::vector<ParticleOffsetInfo> particleOffsets;
    std::vector<ExceptionOffsetInfo> exceptionOffsets;
    std::map<std::pair<int, int>, int> exceptionMap;
    std::vector<std::vector<int>> sliceForceGroup;
};

/**
 * This is an internal class used to record information about a particle.
 * @private
 */
class SlicedPmeForce::ParticleInfo {
public:
    int subset;
    double charge;
    ParticleInfo() {
        charge = 0.0;
        subset = 0;
    }
    ParticleInfo(double charge, int subset) :
        charge(charge), subset(subset) {
    }
};

/**
 * This is an internal class used to record information about an exception.
 * @private
 */
class SlicedPmeForce::ExceptionInfo {
public:
    int particle1, particle2;
    double chargeProd;
    ExceptionInfo() {
        particle1 = particle2 = -1;
        chargeProd = 0.0;
    }
    ExceptionInfo(int particle1, int particle2, double chargeProd) :
        particle1(particle1), particle2(particle2), chargeProd(chargeProd) {
    }
};

/**
 * This is an internal class used to record information about a global parameter.
 * @private
 */
class SlicedPmeForce::GlobalParameterInfo {
public:
    std::string name;
    double defaultValue;
    GlobalParameterInfo() {
    }
    GlobalParameterInfo(const std::string& name, double defaultValue) : name(name), defaultValue(defaultValue) {
    }
};

/**
 * This is an internal class used to record information about a particle parameter offset.
 * @private
 */
class SlicedPmeForce::ParticleOffsetInfo {
public:
    int parameter, particle;
    double chargeScale;
    ParticleOffsetInfo() {
        particle = parameter = -1;
        chargeScale = 0.0;
    }
    ParticleOffsetInfo(int parameter, int particle, double chargeScale) :
        parameter(parameter), particle(particle), chargeScale(chargeScale) {
    }
};

/**
 * This is an internal class used to record information about an exception parameter offset.
 * @private
 */
class SlicedPmeForce::ExceptionOffsetInfo {
public:
    int parameter, exception;
    double chargeProdScale;
    ExceptionOffsetInfo() {
        exception = parameter = -1;
        chargeProdScale = 0.0;
    }
    ExceptionOffsetInfo(int parameter, int exception, double chargeProdScale) :
        parameter(parameter), exception(exception), chargeProdScale(chargeProdScale) {
    }
};

} // namespace OpenMM

#endif /*OPENMM_SLICEDPMEFORCE_H_*/