/* -------------------------------------------------------------------------- *
 *                                   OpenMM                                   *
 * -------------------------------------------------------------------------- *
 * This is part of the OpenMM molecular simulation toolkit originating from   *
 * Simbios, the NIH National Center for Physics-Based Simulation of           *
 * Biological Structures at Stanford, funded under the NIH Roadmap for        *
 * Medical Research, grant U54 GM072970. See https://simtk.org.               *
 *                                                                            *
 * Portions copyright (c) 2008 Stanford University and the Authors.           *
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

/**
 * This tests the Cuda implementation of GBSAOBCForceField.
 */

#include "../../../tests/AssertionUtilities.h"
#include "OpenMMContext.h"
#include "CudaPlatform.h"
#include "GBSAOBCForceField.h"
#include "System.h"
#include "LangevinIntegrator.h"
#include "../src/SimTKUtilities/SimTKOpenMMRealType.h"
#include "../src/sfmt/SFMT.h"
#include "StandardMMForceField.h"
#include <iostream>
#include <vector>

using namespace OpenMM;
using namespace std;

const double TOL = 1e-5;

void testSingleAtom() {
    CudaPlatform platform;
    System system(1, 0);
    system.setAtomMass(0, 2.0);
    LangevinIntegrator integrator(0, 0.1, 0.01);
    GBSAOBCForceField* forceField = new GBSAOBCForceField(1);
    forceField->setAtomParameters(0, 0.5, 0.15, 1);
    system.addForce(forceField);
    system.addForce(new StandardMMForceField(1, 0, 0, 0, 0, 0));
    OpenMMContext context(system, integrator, platform);
    vector<Vec3> positions(1);
    positions[0] = Vec3(0, 0, 0);
    context.setPositions(positions);
    State state = context.getState(State::Energy);
    double bornRadius = 0.15-0.09; // dielectric offset
    double eps0 = EPSILON0;
    double bornEnergy = (-0.5*0.5/(8*PI_M*eps0))*(1.0/forceField->getSoluteDielectric()-1.0/forceField->getSolventDielectric())/bornRadius;
    double extendedRadius = bornRadius+0.14; // probe radius
    double nonpolarEnergy = CAL2JOULE*PI_M*0.0216*(10*extendedRadius)*(10*extendedRadius)*std::pow(0.15/bornRadius, 6.0); // Where did this formula come from?  Just copied it from CpuImplicitSolvent.cpp
    ASSERT_EQUAL_TOL((bornEnergy+nonpolarEnergy), state.getPotentialEnergy(), 0.01);
}

void testForce() {
    CudaPlatform platform;
    const int numAtoms = 10;
    System system(numAtoms, 0);
    LangevinIntegrator integrator(0, 0.1, 0.01);
    GBSAOBCForceField* forceField = new GBSAOBCForceField(numAtoms);
    for (int i = 0; i < numAtoms; ++i)
        forceField->setAtomParameters(i, i%2 == 0 ? -1 : 1, 0.15, 1);
    system.addForce(forceField);
    system.addForce(new StandardMMForceField(numAtoms, 0, 0, 0, 0, 0));
    OpenMMContext context(system, integrator, platform);
    
    // Set random positions for all the atoms.
    
    vector<Vec3> positions(numAtoms);
    init_gen_rand(0);
    for (int i = 0; i < numAtoms; ++i)
        positions[i] = Vec3(5.0*genrand_real2(), 5.0*genrand_real2(), 5.0*genrand_real2());
    context.setPositions(positions);
    State state = context.getState(State::Forces | State::Energy);
    
    // Take a small step in the direction of the energy gradient.
    
    double norm = 0.0;
    for (int i = 0; i < numAtoms; ++i) {
        Vec3 f = state.getForces()[i];
        norm += f[0]*f[0] + f[1]*f[1] + f[2]*f[2];
    }
    norm = std::sqrt(norm);
    const double delta = 1e-3;
    double step = delta/norm;
    for (int i = 0; i < numAtoms; ++i) {
        Vec3 p = positions[i];
        Vec3 f = state.getForces()[i];
        positions[i] = Vec3(p[0]-f[0]*step, p[1]-f[1]*step, p[2]-f[2]*step);
    }
    context.setPositions(positions);
    
    // See whether the potential energy changed by the expected amount.
    
    State state2 = context.getState(State::Energy);
    ASSERT_EQUAL_TOL(norm, (state2.getPotentialEnergy()-state.getPotentialEnergy())/delta, 0.01)
}

int main() {
    try {
        testSingleAtom();
        testForce();
    }
    catch(const exception& e) {
        cout << "exception: " << e.what() << endl;
        return 1;
    }
    cout << "Done" << endl;
    return 0;
}
