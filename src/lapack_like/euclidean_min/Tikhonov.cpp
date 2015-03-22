/*
   Copyright (c) 2009-2015, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/
#include "El.hpp"

namespace El {

template<typename F> 
void Tikhonov
( Orientation orientation,
  const Matrix<F>& A, const Matrix<F>& B, 
  const Matrix<F>& G,       Matrix<F>& X, TikhonovAlg alg )
{
    DEBUG_ONLY(CallStackEntry cse("Tikhonov"))
    const bool normal = ( orientation==NORMAL );
    const Int m = ( normal ? A.Height() : A.Width()  );
    const Int n = ( normal ? A.Width()  : A.Height() );
    if( G.Width() != n )
        LogicError("Tikhonov matrix was the wrong width");
    if( orientation == TRANSPOSE && IsComplex<F>::val )
        LogicError("Transpose version of complex Tikhonov not yet supported");

    if( m >= n )
    {
        Matrix<F> Z;
        if( alg == TIKHONOV_CHOLESKY )
        {
            if( orientation == NORMAL )
                Herk( LOWER, ADJOINT, Base<F>(1), A, Z );
            else
                Herk( LOWER, NORMAL, Base<F>(1), A, Z );
            Herk( LOWER, ADJOINT, Base<F>(1), G, Base<F>(1), Z );
            Cholesky( LOWER, Z );
        }
        else
        {
            const Int mG = G.Height();
            Zeros( Z, m+mG, n );
            auto ZT = Z( IR(0,m),    IR(0,n) );
            auto ZB = Z( IR(m,m+mG), IR(0,n) );
            if( orientation == NORMAL )
                ZT = A;
            else
                Adjoint( A, ZT );
            ZB = G;
            qr::ExplicitTriang( Z ); 
        }
        if( orientation == NORMAL )
            Gemm( ADJOINT, NORMAL, F(1), A, B, X );
        else
            Gemm( NORMAL, NORMAL, F(1), A, B, X );
        cholesky::SolveAfter( LOWER, NORMAL, Z, X );
    }
    else
    {
        LogicError("This case not yet supported");
    }
}

template<typename F> 
void Tikhonov
( Orientation orientation,
  const AbstractDistMatrix<F>& APre, const AbstractDistMatrix<F>& BPre, 
  const AbstractDistMatrix<F>& G,          AbstractDistMatrix<F>& XPre, 
  TikhonovAlg alg )
{
    DEBUG_ONLY(CallStackEntry cse("Tikhonov"))

    auto APtr = ReadProxy<F,MC,MR>( &APre );  auto& A = *APtr;
    auto BPtr = ReadProxy<F,MC,MR>( &BPre );  auto& B = *BPtr;
    auto XPtr = WriteProxy<F,MC,MR>( &XPre ); auto& X = *XPtr;

    const bool normal = ( orientation==NORMAL );
    const Int m = ( normal ? A.Height() : A.Width()  );
    const Int n = ( normal ? A.Width()  : A.Height() );
    if( G.Width() != n )
        LogicError("Tikhonov matrix was the wrong width");
    if( orientation == TRANSPOSE && IsComplex<F>::val )
        LogicError("Transpose version of complex Tikhonov not yet supported");

    if( m >= n )
    {
        DistMatrix<F> Z(A.Grid());
        if( alg == TIKHONOV_CHOLESKY )
        {
            if( orientation == NORMAL )
                Herk( LOWER, ADJOINT, Base<F>(1), A, Z );
            else
                Herk( LOWER, NORMAL, Base<F>(1), A, Z );
            Herk( LOWER, ADJOINT, Base<F>(1), G, Base<F>(1), Z );
            Cholesky( LOWER, Z );
        }
        else
        {
            const Int mG = G.Height();
            Zeros( Z, m+mG, n );
            auto ZT = Z( IR(0,m),    IR(0,n) );
            auto ZB = Z( IR(m,m+mG), IR(0,n) );
            if( orientation == NORMAL )
                ZT = A;
            else
                Adjoint( A, ZT );
            ZB = G;
            qr::ExplicitTriang( Z ); 
        }
        if( orientation == NORMAL )
            Gemm( ADJOINT, NORMAL, F(1), A, B, X );
        else
            Gemm( NORMAL, NORMAL, F(1), A, B, X );
        cholesky::SolveAfter( LOWER, NORMAL, Z, X );
    }
    else
    {
        LogicError("This case not yet supported");
    }
}

// The following routines solve either
//
//   Minimum length: 
//     min_{X,S} || [X;S] ||_F 
//     s.t. [W,G] [X;S] = B, or
//
//   Least squares:  
//     min_X || [W;G] X - [B;0] ||_F,
//
// where W=op(A) is either A, A^T, or A^H, via forming a Hermitian 
// quasi-semidefinite system 
//
//    | alpha*I     0     W | | R/alpha |   | B |
//    |    0     alpha*I  G | | Y/alpha | = | 0 |,
//    |   W^H      G^H    0 | | X       |   | 0 |
//
// when height(W) >= width(W), or
//
//    | alpha*I     0     W^H | | X |   | 0 |
//    |   0      alpha*I  G^H | | S | = | 0 |,
//    |   W         G      0  | | Y |   | B |
//
// when height(W) < width(W).
//
// The latter guarantees that W X + G S = B, X in range(W^H) and 
// S in range(G^H), which shows that [X;S] solves the minimum length problem. 
// The former defines R = B - W X and Y = -G X then ensures that
// [R; Y] is in the null-space of [W; G]^H (therefore solving the least 
// squares problem).
// 
// Note that, ideally, alpha is roughly the minimum (nonzero) singular value
// of [W, G] or [W; G], which implies that the condition number of the 
// quasi-semidefinite system is roughly equal to the condition number of [W, G]
// or [W; G] (see the analysis of Bjorck). If it is too expensive to estimate
// the minimum singular value, and either [W, G] or [W; G] is equilibrated to
// have a unit two-norm, a typical choice for alpha is epsilon^0.25.
//
// The Hermitian quasi-semidefinite systems are solved by converting them into
// Hermitian quasi-definite form via a priori regularization, applying an 
// LDL^H factorization with static pivoting to the regularized system, and
// using the iteratively-refined solution of with the regularized factorization
// as a preconditioner for the original problem (defaulting to Flexible GMRES
// for now).
//
// This approach originated within 
//
//    Michael Saunders, 
//   "Chapter 8, Cholesky-based Methods for Sparse Least Squares:
//    The Benefits of Regularization",
//    in L. Adams and J.L. Nazareth (eds.), Linear and Nonlinear Conjugate
//    Gradient-Related Methods, SIAM, Philadelphia, 92--100 (1996).
//
// But note that SymmLQ and LSQR were used rather than flexible GMRES, and 
// iteratively refining *within* the preconditioner was not discussed.
//

template<typename F>
void Tikhonov
( Orientation orientation,
  const SparseMatrix<F>& A, const Matrix<F>& B,
  const SparseMatrix<F>& G,       Matrix<F>& X, 
  const LeastSquaresCtrl<Base<F>>& ctrl )
{
    DEBUG_ONLY(CallStackEntry cse("Tikhonov"))
    
    // Explicitly form W := op(A)
    // ==========================
    SparseMatrix<F> W;
    if( orientation == NORMAL )
        W = A;
    else if( orientation == TRANSPOSE )
        Transpose( A, W );
    else
        Adjoint( A, W );

    const Int m = W.Height();
    const Int n = W.Width();
    const Int numRHS = B.Width();

    // Embed into a higher-dimensional problem via appending regularization
    // ====================================================================
    SparseMatrix<F> WEmb;
    if( m >= n )
        VCat( W, G, WEmb ); 
    else
        HCat( W, G, WEmb );
    Matrix<F> BEmb;
    Zeros( BEmb, WEmb.Height(), numRHS );
    if( m >= n )
    {
        auto BEmbT = BEmb( IR(0,m), IR(0,numRHS) );
        BEmbT = B;
    }

    // Solve the higher-dimensional problem
    // ====================================
    Matrix<F> XEmb;
    LeastSquares( NORMAL, WEmb, BEmb, XEmb, ctrl );

    // Extract the solution
    // ====================
    if( m >= n )
    {
        X = XEmb;
    }
    else
    {
        X = XEmb( IR(0,n), IR(0,numRHS) ); 
    }
}

template<typename F>
void Tikhonov
( Orientation orientation,
  const DistSparseMatrix<F>& A, const DistMultiVec<F>& B,
  const DistSparseMatrix<F>& G,       DistMultiVec<F>& X, 
  const LeastSquaresCtrl<Base<F>>& ctrl )
{
    DEBUG_ONLY(CallStackEntry cse("Tikhonov"))
    mpi::Comm comm = A.Comm();
    const int commSize = mpi::Size(comm);
    
    // Explicitly form W := op(A)
    // ==========================
    DistSparseMatrix<F> W(comm);
    if( orientation == NORMAL )
        W = A;
    else if( orientation == TRANSPOSE )
        Transpose( A, W );
    else
        Adjoint( A, W );

    const Int m = W.Height();
    const Int n = W.Width();
    const Int numRHS = B.Width();

    // Embed into a higher-dimensional problem via appending regularization
    // ====================================================================
    DistSparseMatrix<F> WEmb(comm);
    if( m >= n )
        VCat( W, G, WEmb ); 
    else
        HCat( W, G, WEmb );
    DistMultiVec<F> BEmb(comm);
    Zeros( BEmb, WEmb.Height(), numRHS );
    if( m >= n )
    {
        // BEmb := [B; 0]
        // --------------
        // TODO: Automate this process
        // Compute the metadata
        // ^^^^^^^^^^^^^^^^^^^^
        vector<int> sendCounts(commSize,0);
        for( Int iLoc=0; iLoc<B.LocalHeight(); ++iLoc )
            sendCounts[ BEmb.RowOwner(B.GlobalRow(iLoc)) ] += numRHS;
        vector<int> recvCounts(commSize);
        mpi::AllToAll( sendCounts.data(), 1, recvCounts.data(), 1, comm );
        vector<int> sendOffs, recvOffs;
        const int totalSend = Scan( sendCounts, sendOffs );
        const int totalRecv = Scan( recvCounts, recvOffs );
        // Pack
        // ^^^^
        auto offs = sendOffs;
        vector<ValueIntPair<F>> sendBuf(totalSend);
        for( Int iLoc=0; iLoc<B.LocalHeight(); ++iLoc )
        {
            const Int i = B.GlobalRow(iLoc);
            const int owner = BEmb.RowOwner(i);
            for( Int j=0; j<numRHS; ++j )
            {
                const F value = B.GetLocal(iLoc,j);
                sendBuf[offs[owner]].indices[0] = i;
                sendBuf[offs[owner]].indices[1] = j;
                sendBuf[offs[owner]].value = value;
                ++offs[owner];
            }
        }
        // Exchange and unpack
        // ^^^^^^^^^^^^^^^^^^^
        vector<ValueIntPair<F>> recvBuf(totalRecv);
        mpi::AllToAll
        ( sendBuf.data(), sendCounts.data(), sendOffs.data(),
          recvBuf.data(), recvCounts.data(), recvOffs.data(), comm );
        for( Int e=0; e<totalRecv; ++e )
            BEmb.UpdateLocal
            ( recvBuf[e].indices[0]-BEmb.FirstLocalRow(), recvBuf[e].indices[1],
              recvBuf[e].value );
    }

    // Solve the higher-dimensional problem
    // ====================================
    DistMultiVec<F> XEmb(comm);
    LeastSquares( NORMAL, WEmb, BEmb, XEmb, ctrl );

    // Extract the solution
    // ====================
    if( m >= n )
    {
        X = XEmb;
    }
    else
    {
        // Extract X from XEmb = [X; S]
        // ----------------------------
        // TODO: Automate this process
        // Compute the metadata
        // ^^^^^^^^^^^^^^^^^^^^
        vector<int> sendCounts(commSize,0);
        for( Int iLoc=0; iLoc<XEmb.LocalHeight(); ++iLoc )
        {
            const Int i = XEmb.GlobalRow(iLoc);
            if( i < n )
                sendCounts[ X.RowOwner(i) ] += numRHS;
            else
                break;
        }
        vector<int> recvCounts(commSize);
        mpi::AllToAll( sendCounts.data(), 1, recvCounts.data(), 1, comm );
        vector<int> sendOffs, recvOffs;
        const int totalSend = Scan( sendCounts, sendOffs );
        const int totalRecv = Scan( recvCounts, recvOffs );
        // Pack
        // ^^^^
        auto offs = sendOffs;
        vector<ValueIntPair<F>> sendBuf(totalSend);
        for( Int iLoc=0; iLoc<XEmb.LocalHeight(); ++iLoc )
        {
            const Int i = XEmb.GlobalRow(iLoc);
            if( i < n )
            {
                const int owner = X.RowOwner(i);
                for( Int j=0; j<numRHS; ++j )
                {
                    const F value = XEmb.GetLocal(iLoc,j);
                    sendBuf[offs[owner]].indices[0] = i;
                    sendBuf[offs[owner]].indices[1] = j;
                    sendBuf[offs[owner]].value = value;
                    ++offs[owner];
                }
            }
            else
                break;
        }
        // Exchange and unpack
        // ^^^^^^^^^^^^^^^^^^^
        vector<ValueIntPair<F>> recvBuf(totalRecv);
        mpi::AllToAll
        ( sendBuf.data(), sendCounts.data(), sendOffs.data(),
          recvBuf.data(), recvCounts.data(), recvOffs.data(), comm );
        for( Int e=0; e<totalRecv; ++e )
            X.UpdateLocal
            ( recvBuf[e].indices[0]-X.FirstLocalRow(), recvBuf[e].indices[1],
              recvBuf[e].value );
    }
}

#define PROTO(F) \
  template void Tikhonov \
  ( Orientation orientation, \
    const Matrix<F>& A, const Matrix<F>& B, \
    const Matrix<F>& G,       Matrix<F>& X, TikhonovAlg alg ); \
  template void Tikhonov \
  ( Orientation orientation, \
    const AbstractDistMatrix<F>& A, const AbstractDistMatrix<F>& B, \
    const AbstractDistMatrix<F>& G,       AbstractDistMatrix<F>& X, \
    TikhonovAlg alg ); \
  template void Tikhonov \
  ( Orientation orientation, \
    const SparseMatrix<F>& A, const Matrix<F>& B, \
    const SparseMatrix<F>& G,       Matrix<F>& X, \
    const LeastSquaresCtrl<Base<F>>& ctrl ); \
  template void Tikhonov \
  ( Orientation orientation, \
    const DistSparseMatrix<F>& A, const DistMultiVec<F>& B, \
    const DistSparseMatrix<F>& G,       DistMultiVec<F>& X, \
    const LeastSquaresCtrl<Base<F>>& ctrl );

#define EL_NO_INT_PROTO
#include "El/macros/Instantiate.h"

} // namespace El
