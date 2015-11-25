/** @file gsAssembler.hpp

    @brief Provides generic assembler routines

    This file is part of the G+Smo library.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.

    Author(s): S. Kleiss, A. Mantzaflaris, J. Sogn
*/

#include <gsAssembler/gsAssembler.h>
#include <gsAssembler/gsGaussRule.h>
#include <gsCore/gsMultiBasis.h>
#include <gsCore/gsDomainIterator.h>
#include <gsCore/gsField.h>
#include <gsUtils/gsPointGrid.h>

#include <gsAssembler/gsVisitorPoisson.h> // Stiffness volume integrals and load vector


namespace gismo
{

template<class T>
void gsAssembler<T>::refresh()
{ }

template<class T>
void gsAssembler<T>::assemble()
{GISMO_NO_IMPLEMENTATION}

template<class T>
void gsAssembler<T>::assemble(const gsMultiPatch<T> & curSolution)
{GISMO_NO_IMPLEMENTATION}

template<class T>
gsAssembler<T> * gsAssembler<T>::create() const
{GISMO_NO_IMPLEMENTATION}

template<class T>
gsAssembler<T> * gsAssembler<T>::clone() const
{GISMO_NO_IMPLEMENTATION}

template<class T>
bool gsAssembler<T>::check()
{
    const gsBoundaryConditions<T> & m_bConditions = m_pde_ptr->bc();
    
    // Check if boundary conditions are OK
    const int np = m_bases.front().nBases();
    for (typename gsBoundaryConditions<T>::const_iterator it = 
             m_bConditions.dirichletBegin() ; it != m_bConditions.dirichletEnd(); ++it )
    {
        GISMO_ENSURE( it->ps.patch < np && it->ps.patch > -1,
                      "Problem: a Dirichlet boundary condition is set " 
                      "on a patch id which does not exist.");
    }

    for (typename gsBoundaryConditions<T>::const_iterator it = 
             m_bConditions.neumannBegin() ; it != m_bConditions.neumannEnd(); ++it )
    {
        GISMO_ENSURE( it->ps.patch < np && it->ps.patch > -1,
                      "Problem: a Neumann boundary condition is set "
                      "on a patch id which does not exist.");
    }

    if ( m_pde_ptr->domain().nPatches() == 0)
        gsWarn<< "No domain given ! \n";

    if ( m_pde_ptr->bc().size() == 0)
        gsWarn<< "No boundary conditions given ! \n";

    return true;
}

template <class T>
void gsAssembler<T>::scalarProblemGalerkinRefresh()
{
    // Check for coherency
    GISMO_ASSERT(this->check(), "Incoherent data in Poisson assembler");

    GISMO_ASSERT(1==m_bases.size(), "Expecting a single discrete space " 
                 "for standard scalar Galerkin");

    // 1. Obtain a map from basis functions to matrix columns and rows
    gsDofMapper mapper;
    m_bases.front().getMapper(m_options.dirStrategy, m_options.intStrategy,
                              this->pde().bc(), mapper, 0);

    if ( 0 == mapper.freeSize() ) // Are there any interior dofs ?
        gsWarn << " No internal DOFs, zero sized system.\n";

    // 2. Create the sparse system
    m_system = gsSparseSystem<T>(mapper);//1,1
    const index_t nz = m_options.numColNz(m_bases[0][0]);
    m_system.reserve(nz, this->pde().numRhs());
}

template<class T>
void gsAssembler<T>::penalizeDirichletDofs(int unk)
{
    GISMO_ASSERT( m_options.dirStrategy == dirichlet::penalize, "Incorrect options");

    static const T PP = 1e9; // magic number

    const gsMultiBasis<T> & mbasis = m_bases[m_system.colBasis(unk)];
    const gsDofMapper     & mapper = m_system.colMapper(unk);
    // Note: dofs in the system, however dof values need to be computed
    const gsDofMapper & bmap = mbasis.getMapper(dirichlet::elimination, 
                                                m_options.intStrategy,
                                                m_pde_ptr->bc(), unk) ;

    GISMO_ENSURE( m_ddof.rows() == mapper.boundarySize() && 
                  m_ddof.cols() == m_pde_ptr->numRhs(), 
                  "The Dirichlet DoFs were not computed.");
    
    // BCs
    for ( typename gsBoundaryConditions<T>::const_iterator
              it = m_pde_ptr->bc().dirichletBegin();
          it != m_pde_ptr->bc().dirichletEnd(); ++it )
    {            
        const gsBasis<T> & basis = mbasis[it->patch()];

        gsMatrix<unsigned> bnd = safe( basis.boundary(it->side() ) ); 
        for (index_t k=0; k!= bnd.size(); ++k)
        {
            // free dof position
            const index_t ii = mapper.index ( bnd(k) , it->patch() );
            // boundary dof position
            const index_t bb = bmap  .bindex( bnd(k) , it->patch() );

            m_system.matrix()(ii,ii) = PP;                  
            m_system.rhs().row(ii)   = PP * m_ddof.row(bb);
        }
    }

    // Corner values
    for ( typename gsBoundaryConditions<T>::const_citerator
              it = m_pde_ptr->bc().cornerBegin();
          it != m_pde_ptr->bc().cornerEnd(); ++it )
    {
        const int i  = mbasis[it->patch].functionAtCorner(it->corner);
        const int ii = mapper.bindex( i , it->patch );
        m_system.matrix()(ii,ii)       = PP;                  
        m_system.rhs().row(ii).setConstant(PP * it->value);
    }
}

template<class T>
void gsAssembler<T>::setFixedDofs(const gsMatrix<T> & coefMatrix, int unk, int patch)
{
    GISMO_ASSERT( m_options.dirValues == dirichlet::user, "Incorrect options");

    const gsMultiBasis<T> & mbasis = m_bases[m_system.colBasis(unk)];
    const gsDofMapper & mapper = 
        dirichlet::elimination == m_options.dirStrategy ? m_system.colMapper(unk) 
        : mbasis.getMapper(dirichlet::elimination, m_options.intStrategy, 
                           m_pde_ptr->bc(), unk) ;
    
    GISMO_ASSERT(m_ddof.rows()==mapper.boundarySize() && 
                 m_ddof.cols() == m_pde_ptr->numRhs(),
                 "Fixed DoFs were not initialized");

    // for every side with a Dirichlet BC
    for ( typename gsBoundaryConditions<T>::const_iterator
              it =  m_pde_ptr->bc().dirichletBegin();
              it != m_pde_ptr->bc().dirichletEnd()  ; ++it )
    {
        const int k = it->patch();
        if ( k == patch )
        {
            // Get indices in the patch on this boundary
            const gsMatrix<unsigned> boundary = 
                safe(mbasis[k].boundary(it->side()));
            
            //gsInfo <<"Setting the value for: "<< boundary.transpose() <<"\n";
            
            for (index_t i=0; i!= boundary.size(); ++i)
            {
                // Note: boundary.at(i) is the patch-local index of a
                // control point on the patch
                const int ii  = mapper.bindex( boundary.at(i) , k );
                
                m_ddof.row(ii) = coefMatrix.row(boundary.at(i));
            }
        }
    }
}

template<class T>
void gsAssembler<T>::setFixedDofVector(gsMatrix<T> & vals, int unk)
{
    m_ddof.swap(vals);
    vals.resize(0,0);
    // Assuming that the DoFs are already set by the user
    GISMO_ENSURE( m_ddof.rows() == m_system.colMapper(unk).boundarySize() && 
                  m_ddof.cols() == m_pde_ptr->numRhs(), 
                  "The Dirichlet DoFs were not provided correctly.");
}


template<class T>
void gsAssembler<T>::computeDirichletDofs(int unk)
{
    if ( m_options.dirStrategy == dirichlet::nitsche)
        return; // Nothing to compute

    const gsMultiBasis<T> & mbasis = m_bases[m_system.colBasis(unk)];
    const gsDofMapper & mapper = 
        dirichlet::elimination == m_options.dirStrategy ? m_system.colMapper(unk) 
        : mbasis.getMapper(dirichlet::elimination, m_options.intStrategy, 
                           m_pde_ptr->bc(), unk);

    switch (m_options.dirValues)
    {
    case dirichlet::homogeneous:
        // If we have a homogeneous Dirichlet problem fill boundary
        // DoFs with zeros
        m_ddof.setZero(mapper.boundarySize(), m_pde_ptr->numRhs() );
        break;
    case dirichlet::interpolation:        
        computeDirichletDofsIntpl(mapper, mbasis);
        break;
    case dirichlet::l2Projection:
        computeDirichletDofsL2Proj(mapper, mbasis);
        break;
    case dirichlet::user :
        // Assuming that the DoFs are already set by the user
        GISMO_ENSURE( m_ddof.rows() == mapper.boundarySize() && 
                      m_ddof.cols() == m_pde_ptr->numRhs(), 
                      "The Dirichlet DoFs are not set.");
        break;
    default:
        GISMO_ERROR("Something went wrong with Dirichlet values.");
    }
    
    // Corner values
    for ( typename gsBoundaryConditions<T>::const_citerator
              it = m_pde_ptr->bc().cornerBegin();
          it != m_pde_ptr->bc().cornerEnd(); ++it )
    {
        const int i  = mbasis[it->patch].functionAtCorner(it->corner);
        const int ii = mapper.bindex( i , it->patch );
        m_ddof.row(ii).setConstant(it->value);
    }
}


// SKleiss: Note that this implementation is not useable for (T)HB-Splines!
//
// 1. Computation of the Dirichlet values explicitly uses gsPointGrid(rr)
// Computing a grid of evaluation points does not make sense for any locally
// refined basis.
// Also, "component(i)" is used.
// I'm afraid this makes sense ONLY FOR TENSOR-PRODUCT bases.
//
// 2. As of now (16.May 2014), the boundaryBasis of (T)HB-spline basis is not
// implemented, as far as I know.
//
// 3. gsInterpolate uses the anchors of the boundary basis.
// With truncated hierarchical B-splines, the use of classical anchors does
// not work, because functions might be truncated to zero at these points.
template<class T> // 
void gsAssembler<T>::computeDirichletDofsIntpl(const gsDofMapper & mapper,
                                               const gsMultiBasis<T> & mbasis)
{
    m_ddof.resize(mapper.boundarySize(), m_pde_ptr->numRhs() );

    // Iterate over all patch-sides with Dirichlet-boundary conditions
    for ( typename gsBoundaryConditions<T>::const_iterator
              it = m_pde_ptr->bc().dirichletBegin();
          it != m_pde_ptr->bc().dirichletEnd(); ++it )
    {
        //const int unk = it->unknown();
        const int k   = it->patch();
        const gsBasis<T> & basis = mbasis[k];

        // Get dofs on this boundary
        const gsMatrix<unsigned> boundary = safe(basis.boundary(it->side()));

        // If the condition is homogeneous then fill with zeros
        if ( it->isHomogeneous() )
        {
            for (index_t i=0; i!= boundary.size(); ++i)
            {
                const int ii= mapper.bindex( boundary.at(i) , k );
                m_ddof.row(ii).setZero();
            }
            continue;
        }

        // Get the side information
        int dir = it->side().direction( );
        index_t param = (it->side().parameter() ? 1 : 0);

        // Compute grid of points on the face ("face anchors")
        std::vector< gsVector<T> > rr;
        rr.reserve( this->patches().parDim() );

        for ( int i=0; i < this->patches().parDim(); ++i)
        {
            if ( i==dir )
            {
                gsVector<T> b(1); 
                b[0] = ( basis.component(i).support() ) (0, param);
                rr.push_back(b);
            }
            else
            {   
                rr.push_back( basis.component(i).anchors()->transpose() );
            }
        }

        GISMO_ASSERT(it->function()->targetDim() == m_pde_ptr->numRhs(),
                     "Given Dirichlet boundary function does not match problem dimension.\n");

        // Compute dirichlet values
        gsMatrix<T> fpts;
        if ( it->parametric() )
            fpts = it->function()->eval( gsPointGrid( rr ) );
        else
            fpts = it->function()->eval( m_pde_ptr->domain()[it->patch()].eval(  gsPointGrid( rr ) ) );

        // Interpolate dirichlet boundary 
        gsBasis<T> * h = basis.boundaryBasis(it->side());
        gsGeometry<T> * geo = h->interpolateAtAnchors(fpts);
        const gsMatrix<T> & dVals =  geo->coefs();

        // Save corresponding boundary dofs
        for (index_t l=0; l!= boundary.size(); ++l)
        {
            const int ii = mapper.bindex( boundary.at(l) , it->patch() );

            m_ddof.row(ii) = dVals.row(l);
        }

        delete h;
        delete geo;
    }
}

template<class T>
void gsAssembler<T>::computeDirichletDofsL2Proj(const gsDofMapper & mapper,
                                                const gsMultiBasis<T> & mbasis)
{
    m_ddof.resize( mapper.boundarySize(), m_pde_ptr->numRhs() );

    // Set up matrix, right-hand-side and solution vector/matrix for
    // the L2-projection
    gsSparseEntries<T> projMatEntries;
    gsMatrix<T>        globProjRhs;   
    globProjRhs.setZero( mapper.boundarySize(), m_pde_ptr->numRhs() );

    // Temporaries
    gsMatrix<T> quNodes;
    gsVector<T> quWeights;

    gsMatrix<T> rhsVals;
    gsMatrix<unsigned> globIdxAct;
    gsMatrix<T> basisVals;

    // Iterate over all patch-sides with Dirichlet-boundary conditions
    for ( typename gsBoundaryConditions<T>::const_iterator
              iter = m_pde_ptr->bc().dirichletBegin();
          iter != m_pde_ptr->bc().dirichletEnd(); ++iter )
    {
        const int unk = iter->unknown();
        const int patchIdx   = iter->patch();
        const gsBasis<T> & basis = (m_bases[unk])[patchIdx];

        typename gsGeometry<T>::Evaluator geoEval( m_pde_ptr->domain()[patchIdx].evaluator(NEED_MEASURE));

        // Set up quadrature to degree+1 Gauss points per direction,
        // all lying on iter->side() except from the direction which
        // is NOT along the element
        gsGaussRule<T> bdQuRule(basis, 1.0, 1, iter->side().direction());

        // Create the iterator along the given part boundary.
        typename gsBasis<T>::domainIter bdryIter = basis.makeDomainIterator(iter->side());

        for(; bdryIter->good(); bdryIter->next() )
        {
            bdQuRule.mapTo( bdryIter->lowerCorner(), bdryIter->upperCorner(),
                          quNodes, quWeights);

            geoEval->evaluateAt( quNodes );

            // the values of the boundary condition are stored
            // to rhsVals. Here, "rhs" refers to the right-hand-side
            // of the L2-projection, not of the PDE.
            rhsVals = iter->function()->eval( m_pde_ptr->domain()[patchIdx].eval( quNodes ) );

            basis.eval_into( quNodes, basisVals);

            // Indices involved here:
            // --- Local index:
            // Index of the basis function/DOF on the patch.
            // Does not take into account any boundary or interface conditions.
            // --- Global Index:
            // Each DOF has a unique global index that runs over all patches.
            // This global index includes a re-ordering such that all eliminated
            // DOFs come at the end.
            // The global index also takes care of glued interface, i.e., corresponding
            // DOFs on different patches will have the same global index, if they are
            // glued together.
            // --- Boundary Index (actually, it's a "Dirichlet Boundary Index"):
            // The eliminated DOFs, which come last in the global indexing,
            // have their own numbering starting from zero.

            // Get the global indices (second line) of the local
            // active basis (first line) functions/DOFs:
            basis.active_into(quNodes.col(0), globIdxAct );
            mapper.localToGlobal( globIdxAct, patchIdx, globIdxAct);

            // Out of the active functions/DOFs on this element, collect all those
            // which correspond to a boundary DOF.
            // This is checked by calling mapper.is_boundary_index( global Index )

            // eltBdryFcts stores the row in basisVals/globIdxAct, i.e.,
            // something like a "element-wise index"
            std::vector<index_t> eltBdryFcts;
            eltBdryFcts.reserve(mapper.boundarySize());
            for( index_t i=0; i < globIdxAct.rows(); i++)
                if( mapper.is_boundary_index( globIdxAct(i,0)) )
                    eltBdryFcts.push_back( i );

            // Do the actual assembly:
            for( index_t k=0; k < quNodes.cols(); k++ )
            {
                const T weight_k = quWeights[k] * geoEval->measure(k);

                // Only run through the active boundary functions on the element:
                for( size_t i0=0; i0 < eltBdryFcts.size(); i0++ )
                {
                    // Each active boundary function/DOF in eltBdryFcts has...
                    // ...the above-mentioned "element-wise index"
                    const unsigned i = eltBdryFcts[i0];
                    // ...the boundary index.
                    const unsigned ii = mapper.global_to_bindex( globIdxAct( i ));

                    for( size_t j0=0; j0 < eltBdryFcts.size(); j0++ )
                    {
                        const unsigned j = eltBdryFcts[j0];
                        const unsigned jj = mapper.global_to_bindex( globIdxAct( j ));

                        // Use the "element-wise index" to get the needed
                        // function value.
                        // Use the boundary index to put the value in the proper
                        // place in the global projection matrix.
                        projMatEntries.add(ii, jj, weight_k * basisVals(i,k) * basisVals(j,k));
                    } // for j

                    globProjRhs.row(ii) += weight_k *  basisVals(i,k) * rhsVals.col(k).transpose();

                } // for i
            } // for k
        } // bdryIter
    } // boundaryConditions-Iterator

    gsSparseMatrix<T> globProjMat( mapper.boundarySize(), mapper.boundarySize() );
    globProjMat.setFrom( projMatEntries );
    globProjMat.makeCompressed();

    // Solve the linear system:
    // The position in the solution vector already corresponds to the
    // numbering by the boundary index. Hence, we can simply take them
    // for the values of the eliminated Dirichlet DOFs.
    typename gsSparseSolver<T>::CGDiagonal solver;
    m_ddof = solver.compute( globProjMat ).solve ( globProjRhs );
    
} // computeDirichletDofsL2Proj

template<class T>
void gsAssembler<T>::constructSolution(const gsMatrix<T>& solVector, 
                                       gsMultiPatch<T>& result, int unk) const
{
    // we might need to get a result even without having the system ..
    //GISMO_ASSERT(m_dofs == m_rhs.rows(), "Something went wrong, assemble() not called?");

    // fixme: based on \a m_options and \a unk choose the right dof mapper
    const gsDofMapper & mapper = m_system.colMapper(unk);

    GISMO_ASSERT(solVector.rows() == mapper.freeSize(), "Something went wrong, solution vector is not OK.");

    result.clear(); // result is cleared first
    
    const index_t dim = m_pde_ptr->numRhs();
    
    for (size_t p=0; p < m_pde_ptr->domain().nPatches(); ++p )
    {    
        // Reconstruct solution coefficients on patch p
        const int sz  = m_bases[unk][p].size();
        gsMatrix<T> coeffs( sz, dim);

        for (index_t i = 0; i < sz; ++i)
        {
            if ( mapper.is_free(i, p) ) // DoF value is in the solVector
            {
                coeffs.row(i) = solVector.row( mapper.index(i, p) );
            }
            else // eliminated DoF: fill with Dirichlet data
            {
                coeffs.row(i) = m_ddof.row( mapper.bindex(i, p) );
            }
        }
        
        result.addPatch( m_bases[unk][p].makeGeometry( give(coeffs) ) );
    }

    // AM: result topology ?
}

template<class T>
gsField<T> *  gsAssembler<T>::constructSolution(const gsMatrix<T>& solVector,
                                                int unk) const
{
    const gsDofMapper & mapper = m_system.colMapper(unk);

    std::vector<gsFunction<T> * > sols ;

    const index_t dim = m_pde_ptr->numRhs();
    gsMatrix<T> coeffs;

    for (size_t p=0; p < m_pde_ptr->domain().nPatches(); ++p )
    {
        // Reconstruct solution coefficients on patch p
        const int sz  = m_bases[unk][p].size();
        coeffs.resize( sz, dim);

        for (index_t i = 0; i < sz; ++i)
        {
            if ( mapper.is_free(i, p) ) // DoF value is in the solVector
            {
                coeffs.row(i) = solVector.row( mapper.index(i, p) );
            }
            else // eliminated DoF: fill with Dirichlet data
            {
                coeffs.row(i) = m_ddof.row( mapper.bindex(i, p) );
            }
        }

        sols.push_back( m_bases[unk][p].makeGeometry( give(coeffs) ) );
    }

    return new gsField<T>(m_pde_ptr->domain(), sols);
}

}// namespace gismo