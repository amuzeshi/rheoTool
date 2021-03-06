/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright held by original author
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the
    Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM; if not, write to the Free Software Foundation,
    Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

\*---------------------------------------------------------------------------*/

#include "XPomPomLog.H"
#include "addToRunTimeSelectionTable.H"


// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

namespace Foam
{
namespace constitutiveEqs 
{
    defineTypeNameAndDebug(XPomPomLog, 0);
    addToRunTimeSelectionTable(constitutiveEq, XPomPomLog, dictionary);
}
}

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::constitutiveEqs::XPomPomLog::XPomPomLog
(
    const word& name,
    const volVectorField& U,
    const surfaceScalarField& phi,
    const dictionary& dict
)
:
    constitutiveEq(name, U, phi),
    tau_
    (
        IOobject
        (
            "tau" + name,
            U.time().timeName(),
            U.mesh(),
            IOobject::MUST_READ,
            IOobject::AUTO_WRITE
        ),
        U.mesh()
    ),
    theta_
    (
        IOobject
        (
            "theta" + name,
            U.time().timeName(),
            U.mesh(),
            IOobject::MUST_READ,
            IOobject::AUTO_WRITE
        ),
        U.mesh()
    ),
    eigVals_
    (
        IOobject
        (
            "eigVals" + name,
            U.time().timeName(),
            U.mesh(),
            IOobject::READ_IF_PRESENT,
            IOobject::AUTO_WRITE
        ),
        U.mesh(),
        dimensionedTensor
        (
                "I",
                dimless,
                pTraits<tensor>::I
        ),
        extrapolatedCalculatedFvPatchField<tensor>::typeName
    ),
    eigVecs_
    (
        IOobject
        (
            "eigVecs" + name,
            U.time().timeName(),
            U.mesh(),
            IOobject::READ_IF_PRESENT,
            IOobject::AUTO_WRITE
        ),
        U.mesh(),
        dimensionedTensor
        (
                "I",
                dimless,
                pTraits<tensor>::I
        ),
        extrapolatedCalculatedFvPatchField<tensor>::typeName
    ),
    rho_(dict.lookup("rho")),
    etaS_(dict.lookup("etaS")),
    etaP_(dict.lookup("etaP")),
    lambdaS_(dict.lookup("lambdaS")),
    lambdaB_(dict.lookup("lambdaB")),
    alpha_(dict.lookup("alpha")),
    q_(dict.lookup("q"))
{
 checkForStab(dict);
}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

void Foam::constitutiveEqs::XPomPomLog::correct()
{
    // Decompose grad(U).T()

    volTensorField L = fvc::grad(U());

    dimensionedScalar c1( "zero", dimensionSet(0, 0, -1, 0, 0, 0, 0), 0.);
    volTensorField   B = c1 * eigVecs_; 
    volTensorField   omega = B;
    volTensorField   M = (eigVecs_.T() & L.T() & eigVecs_);

    decomposeGradU(M, eigVals_, eigVecs_, omega, B);
  
    // Solve the constitutive Eq in theta = log(c)
 
    dimensionedTensor Itensor
    ( 
        "Identity", 
        dimensionSet(0, 0, 0, 0, 0, 0, 0), 
        tensor::I 
    );
    
    volSymmTensorField A_(symm(eigVecs_ & eigVals_ & eigVecs_.T()));
    
    volScalarField trA(tr(A_));
    
    volScalarField lambda(Foam::sqrt(trA/3.));
    
    volScalarField f
    ( 
        2.*(lambdaB_/lambdaS_)*Foam::exp( (2./q_)*(lambda-1.) ) * (1. - 1./lambda)
     + (1./(lambda*lambda)) * ( 1. - alpha_ - (alpha_/3.) * ( tr(A_&A_) - 2.*trA ) )
    );

    fvSymmTensorMatrix thetaEqn
    (
         fvm::ddt(theta_)
       + fvm::div(phi(), theta_)
       ==
       symm
       (  
         (omega&theta_)
       - (theta_&omega)
       + 2. * B
       - (1./lambdaB_) * 
         ( 
           (
              eigVecs_ &
                (
                  inv(eigVals_)  
                )
            & eigVecs_.T() 
           ) &
           ( A_*(f-2.*alpha_) + alpha_*(A_&A_) + Itensor*(alpha_ - 1.)    )
         )
       ) 
       
    );
   
    thetaEqn.relax();
    thetaEqn.solve();
  
    // Diagonalization of theta

    calcEig(theta_, eigVals_, eigVecs_);

    // Convert from theta to tau

    tau_ = (etaP_/lambdaB_) * symm( (eigVecs_ & eigVals_ & eigVecs_.T()) - Itensor);

    tau_.correctBoundaryConditions();
}


// ************************************************************************* //
