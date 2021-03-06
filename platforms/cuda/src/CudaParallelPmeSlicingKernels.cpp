/* -------------------------------------------------------------------------- *
 *                                   OpenMM                                   *
 * -------------------------------------------------------------------------- *
 * This is part of the OpenMM molecular simulation toolkit originating from   *
 * Simbios, the NIH National Center for Physics-Based Simulation of           *
 * Biological Structures at Stanford, funded under the NIH Roadmap for        *
 * Medical Research, grant U54 GM072970. See https://simtk.org.               *
 *                                                                            *
 * Portions copyright (c) 2011-2021 Stanford University and the Authors.      *
 * Authors: Peter Eastman                                                     *
 * Contributors:                                                              *
 *                                                                            *
 * This program is free software: you can redistribute it and/or modify       *
 * it under the terms of the GNU Lesser General Public License as published   *
 * by the Free Software Foundation, either version 3 of the License, or       *
 * (at your option) any later version.                                        *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU Lesser General Public License for more details.                        *
 *                                                                            *
 * You should have received a copy of the GNU Lesser General Public License   *
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.      *
 * -------------------------------------------------------------------------- */

#include "CudaParallelPmeSlicingKernels.h"
#include "CudaPmeSlicingKernelSources.h"
#include "openmm/common/ContextSelector.h"

using namespace PmeSlicing;
using namespace OpenMM;
using namespace std;

class CudaParallelCalcSlicedPmeForceKernel::Task : public CudaContext::WorkTask {
public:
    Task(ContextImpl& context, CudaCalcSlicedPmeForceKernel& kernel, bool includeForce,
            bool includeEnergy, bool includeDirect, bool includeReciprocal, double& energy) : context(context), kernel(kernel),
            includeForce(includeForce), includeEnergy(includeEnergy), includeDirect(includeDirect), includeReciprocal(includeReciprocal), energy(energy) {
    }
    void execute() {
        energy += kernel.execute(context, includeForce, includeEnergy, includeDirect, includeReciprocal);
    }
private:
    ContextImpl& context;
    CudaCalcSlicedPmeForceKernel& kernel;
    bool includeForce, includeEnergy, includeDirect, includeReciprocal;
    double& energy;
};

CudaParallelCalcSlicedPmeForceKernel::CudaParallelCalcSlicedPmeForceKernel(std::string name, const Platform& platform, CudaPlatform::PlatformData& data, const System& system) :
        CalcSlicedPmeForceKernel(name, platform), data(data) {
    for (int i = 0; i < (int) data.contexts.size(); i++)
        kernels.push_back(Kernel(new CudaCalcSlicedPmeForceKernel(name, platform, *data.contexts[i], system)));
}

void CudaParallelCalcSlicedPmeForceKernel::initialize(const System& system, const SlicedPmeForce& force) {
    for (int i = 0; i < (int) kernels.size(); i++)
        getKernel(i).initialize(system, force);
}

double CudaParallelCalcSlicedPmeForceKernel::execute(ContextImpl& context, bool includeForces, bool includeEnergy, bool includeDirect, bool includeReciprocal) {
    for (int i = 0; i < (int) data.contexts.size(); i++) {
        CudaContext& cu = *data.contexts[i];
        ComputeContext::WorkThread& thread = cu.getWorkThread();
        thread.addTask(new Task(context, getKernel(i), includeForces, includeEnergy, includeDirect, includeReciprocal, data.contextEnergy[i]));
    }
    return 0.0;
}

void CudaParallelCalcSlicedPmeForceKernel::copyParametersToContext(ContextImpl& context, const SlicedPmeForce& force) {
    for (int i = 0; i < (int) kernels.size(); i++)
        getKernel(i).copyParametersToContext(context, force);
}

void CudaParallelCalcSlicedPmeForceKernel::getPMEParameters(double& alpha, int& nx, int& ny, int& nz) const {
    dynamic_cast<const CudaCalcSlicedPmeForceKernel&>(kernels[0].getImpl()).getPMEParameters(alpha, nx, ny, nz);
}
