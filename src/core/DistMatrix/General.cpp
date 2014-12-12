/*
   Copyright (c) 2009-2014, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/
#include "El.hpp"

namespace El {

// Public section
// ##############

// Constructors and destructors
// ============================

template<typename T,Dist U,Dist V>
GeneralDistMatrix<T,U,V>::GeneralDistMatrix( const El::Grid& grid, Int root )
: AbstractDistMatrix<T>(grid,root)
{ }

template<typename T,Dist U,Dist V>
GeneralDistMatrix<T,U,V>::GeneralDistMatrix( GeneralDistMatrix<T,U,V>&& A ) 
EL_NOEXCEPT
: AbstractDistMatrix<T>(std::move(A))
{ }

// Assignment and reconfiguration
// ==============================

template<typename T,Dist U,Dist V>
GeneralDistMatrix<T,U,V>& 
GeneralDistMatrix<T,U,V>::operator=( GeneralDistMatrix<T,U,V>&& A )
{
    AbstractDistMatrix<T>::operator=( std::move(A) );
    return *this;
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::AlignColsWith
( const El::DistData& data, bool constrain, bool allowMismatch )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::AlignColsWith")) 
    this->SetGrid( *data.grid );
    this->SetRoot( data.root );
    if( data.colDist == U || data.colDist == UPart )
        this->AlignCols( data.colAlign, constrain );
    else if( data.rowDist == U || data.rowDist == UPart )
        this->AlignCols( data.rowAlign, constrain );
    else if( data.colDist == UScat )
        this->AlignCols( data.colAlign % this->ColStride(), constrain );
    else if( data.rowDist == UScat )
        this->AlignCols( data.rowAlign % this->ColStride(), constrain );
    else if( U != UGath && data.colDist != UGath && data.rowDist != UGath &&
            !allowMismatch ) 
        LogicError("Nonsensical alignment");
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::AlignRowsWith
( const El::DistData& data, bool constrain, bool allowMismatch )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::AlignRowsWith")) 
    this->SetGrid( *data.grid );
    this->SetRoot( data.root );
    if( data.colDist == V || data.colDist == VPart )
        this->AlignRows( data.colAlign, constrain );
    else if( data.rowDist == V || data.rowDist == VPart )
        this->AlignRows( data.rowAlign, constrain );
    else if( data.colDist == VScat )
        this->AlignRows( data.colAlign % this->RowStride(), constrain );
    else if( data.rowDist == VScat )
        this->AlignRows( data.rowAlign % this->RowStride(), constrain );
    else if( V != VGath && data.colDist != VGath && data.rowDist != VGath &&
             !allowMismatch ) 
        LogicError("Nonsensical alignment");
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::SumScatterFrom( const DistMatrix<T,UGath,VGath>& A )
{
    DEBUG_ONLY(
        CallStackEntry cse("GDM::SumScatterFrom");
        AssertSameGrids( *this, A );
    )
    this->Resize( A.Height(), A.Width() );
    // NOTE: This will be *slightly* slower than necessary due to the result
    //       of the MPI operations being added rather than just copied
    Zeros( this->Matrix(), this->LocalHeight(), this->LocalWidth() );
    this->SumScatterUpdate( T(1), A );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::PartialRowSumScatterFrom
( const DistMatrix<T,U,VPart>& A )
{
    DEBUG_ONLY(
        CallStackEntry cse("GDM::PartialRowSumScatterFrom");
        AssertSameGrids( *this, A );
    )
    this->AlignAndResize
    ( A.ColAlign(), A.RowAlign(), A.Height(), A.Width(), false, false );
    // NOTE: This will be *slightly* slower than necessary due to the result
    //       of the MPI operations being added rather than just copied
    Zeros( this->Matrix(), this->LocalHeight(), this->LocalWidth() );
    this->PartialRowSumScatterUpdate( T(1), A );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::PartialColSumScatterFrom
( const DistMatrix<T,UPart,V>& A )
{
    DEBUG_ONLY(
        CallStackEntry cse("GDM::PartialColSumScatterFrom");
        AssertSameGrids( *this, A );
    )
    this->AlignAndResize
    ( A.ColAlign(), A.RowAlign(), A.Height(), A.Width(), false, false );
    // NOTE: This will be *slightly* slower than necessary due to the result
    //       of the MPI operations being added rather than just copied
    Zeros( this->Matrix(), this->LocalHeight(), this->LocalWidth() );
    this->PartialColSumScatterUpdate( T(1), A );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::SumScatterUpdate
( T alpha, const DistMatrix<T,UGath,VGath>& A )
{
    DEBUG_ONLY(
        CallStackEntry cse("GDM::SumScatterUpdate");
        AssertSameGrids( *this, A );
        this->AssertNotLocked();
        this->AssertSameSize( A.Height(), A.Width() );
    )
    if( !this->Participating() )
        return;

    const Int colStride = this->ColStride();
    const Int rowStride = this->RowStride();
    const Int colAlign = this->ColAlign();
    const Int rowAlign = this->RowAlign();

    const Int height = this->Height();
    const Int width = this->Width();
    const Int localHeight = this->LocalHeight();
    const Int localWidth = this->LocalWidth();
    const Int maxLocalHeight = MaxLength(height,colStride);
    const Int maxLocalWidth = MaxLength(width,rowStride);

    const Int recvSize = mpi::Pad( maxLocalHeight*maxLocalWidth );
    const Int sendSize = colStride*rowStride*recvSize;

    // Pack 
    T* buffer = this->auxMemory_.Require( sendSize );
    EL_OUTER_PARALLEL_FOR
    for( Int l=0; l<rowStride; ++l )
    {
        const Int thisRowShift = Shift_( l, rowAlign, rowStride );
        const Int thisLocalWidth = Length_( width, thisRowShift, rowStride );
        for( Int k=0; k<colStride; ++k )
        {
            T* data = &buffer[(k+l*colStride)*recvSize];
            const Int thisColShift = Shift_( k, colAlign, colStride );
            const Int thisLocalHeight = Length_(height,thisColShift,colStride);
            InterleaveMatrix
            ( thisLocalHeight, thisLocalWidth,
              A.LockedBuffer(thisColShift,thisRowShift), colStride, A.LDim(),
              data, 1, thisLocalHeight );
        }
    }

    // Communicate
    mpi::ReduceScatter( buffer, recvSize, this->DistComm() );

    // Unpack our received data
    InterleaveMatrixUpdate
    ( alpha, localHeight, localWidth,
      buffer,         1, localHeight,
      this->Buffer(), 1, this->LDim() );
    this->auxMemory_.Release();
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::PartialRowSumScatterUpdate
( T alpha, const DistMatrix<T,U,VPart>& A )
{
    DEBUG_ONLY(
        CallStackEntry cse("GDM::PartialRowSumScatterUpdate");
        AssertSameGrids( *this, A );
        this->AssertNotLocked();
        this->AssertSameSize( A.Height(), A.Width() );
    )
    if( !this->Participating() )
        return;

    if( this->RowAlign() % A.RowStride() == A.RowAlign() )
    {
        const Int rowStride = this->RowStride();
        const Int rowStridePart = this->PartialRowStride();
        const Int rowStrideUnion = this->PartialUnionRowStride();
        const Int rowRankPart = this->PartialRowRank();
        const Int rowAlign = this->RowAlign();
        const Int rowShiftOfA = A.RowShift();

        const Int height = this->Height();
        const Int width = this->Width();
        const Int localWidth = this->LocalWidth();
        const Int maxLocalWidth = MaxLength( width, rowStride );
        const Int recvSize = mpi::Pad( height*maxLocalWidth );
        const Int sendSize = rowStrideUnion*recvSize;

        // Pack
        T* buffer = this->auxMemory_.Require( sendSize );
        EL_OUTER_PARALLEL_FOR
        for( Int k=0; k<rowStrideUnion; ++k )
        {
            T* data = &buffer[k*recvSize];
            const Int thisRank = rowRankPart+k*rowStridePart;
            const Int thisRowShift = Shift_( thisRank, rowAlign, rowStride );
            const Int thisRowOffset = 
                (thisRowShift-rowShiftOfA) / rowStridePart;
            const Int thisLocalWidth = 
                Length_( width, thisRowShift, rowStride );
            InterleaveMatrix
            ( height, thisLocalWidth,
              A.LockedBuffer(0,thisRowOffset), 1, rowStrideUnion*A.LDim(),
              data,                            1, height );
        }
    
        // Communicate
        mpi::ReduceScatter( buffer, recvSize, this->PartialUnionRowComm() );

        // Unpack our received data
        InterleaveMatrixUpdate
        ( alpha, height, localWidth,
          buffer,         1, height,
          this->Buffer(), 1, this->LDim() );
        this->auxMemory_.Release();
    }
    else
        LogicError("Unaligned PartialRowSumScatterUpdate not implemented");
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::PartialColSumScatterUpdate
( T alpha, const DistMatrix<T,UPart,V>& A )
{
    DEBUG_ONLY(
        CallStackEntry cse("GDM::PartialColSumScatterUpdate");
        AssertSameGrids( *this, A );
        this->AssertNotLocked();
        this->AssertSameSize( A.Height(), A.Width() );
    )
    if( !this->Participating() )
        return;

#ifdef EL_CACHE_WARNINGS
    if( A.Width() != 1 && A.Grid().Rank() == 0 )
    {
        std::cerr <<
          "PartialColSumScatterUpdate potentially causes a large amount"
          " of cache-thrashing. If possible, avoid it by forming the "
          "(conjugate-)transpose of the [UGath,* ] matrix instead." 
          << std::endl;
    }
#endif
    if( this->ColAlign() % A.ColStride() == A.ColAlign() )
    {
        const Int colStride = this->ColStride();
        const Int colStridePart = this->PartialColStride();
        const Int colStrideUnion = this->PartialUnionColStride();
        const Int colRankPart = this->PartialColRank();
        const Int colAlign = this->ColAlign();
        const Int colShiftOfA = A.ColShift();

        const Int height = this->Height();
        const Int width = this->Width();
        const Int localHeight = this->LocalHeight();
        const Int maxLocalHeight = MaxLength( height, colStride );
        const Int recvSize = mpi::Pad( maxLocalHeight*width );
        const Int sendSize = colStrideUnion*recvSize;

        T* buffer = this->auxMemory_.Require( sendSize );

        // Pack
        EL_OUTER_PARALLEL_FOR
        for( Int k=0; k<colStrideUnion; ++k )
        {
            T* data = &buffer[k*recvSize];
            const Int thisRank = colRankPart+k*colStridePart;
            const Int thisColShift = Shift_( thisRank, colAlign, colStride );
            const Int thisColOffset = 
                (thisColShift-colShiftOfA) / colStridePart;
            const Int thisLocalHeight = 
                Length_( height, thisColShift, colStride );
            InterleaveMatrix
            ( thisLocalHeight, width,
              A.LockedBuffer(thisColOffset,0), colStrideUnion, A.LDim(),
              data, 1, thisLocalHeight );
        }

        // Communicate
        mpi::ReduceScatter( buffer, recvSize, this->PartialUnionColComm() );

        // Unpack our received data
        InterleaveMatrixUpdate
        ( alpha, localHeight, width,
          buffer,         1, localHeight,
          this->Buffer(), 1, this->LDim() );
        this->auxMemory_.Release();
    }
    else
    {
        LogicError("Unaligned PartialColSumScatterUpdate not implemented");
    }
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::TransposeColAllGather
( DistMatrix<T,V,UGath>& A, bool conjugate ) const
{
    DEBUG_ONLY(CallStackEntry cse("GDM::TransposeColAllGather"))
    DistMatrix<T,V,U> ATrans( this->Grid() );
    ATrans.AlignWith( *this );
    ATrans.Resize( this->Width(), this->Height() );
    Transpose( this->LockedMatrix(), ATrans.Matrix(), conjugate );
    copy::RowAllGather( ATrans, A );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::TransposePartialColAllGather
( DistMatrix<T,V,UPart>& A, bool conjugate ) const
{
    DEBUG_ONLY(CallStackEntry cse("GDM::TransposePartialColAllGather"))
    DistMatrix<T,V,U> ATrans( this->Grid() );
    ATrans.AlignWith( *this );
    ATrans.Resize( this->Width(), this->Height() );
    Transpose( this->LockedMatrix(), ATrans.Matrix(), conjugate );
    copy::PartialRowAllGather( ATrans, A );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::AdjointColAllGather( DistMatrix<T,V,UGath>& A ) const
{
    DEBUG_ONLY(CallStackEntry cse("GDM::AdjointRowAllGather"))
    this->TransposeColAllGather( A, true );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::AdjointPartialColAllGather
( DistMatrix<T,V,UPart>& A ) const
{
    DEBUG_ONLY(CallStackEntry cse("GDM::AdjointPartialColAllGather"))
    this->TransposePartialColAllGather( A, true );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::TransposeColFilterFrom
( const DistMatrix<T,V,UGath>& A, bool conjugate )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::TransposeColFilterFrom"))
    DistMatrix<T,V,U> AFilt( A.Grid() );
    if( this->ColConstrained() )
        AFilt.AlignRowsWith( *this, false );
    if( this->RowConstrained() )
        AFilt.AlignColsWith( *this, false );
    copy::RowFilter( A, AFilt );
    if( !this->ColConstrained() )
        this->AlignColsWith( AFilt, false );
    if( !this->RowConstrained() )
        this->AlignRowsWith( AFilt, false );
    this->Resize( A.Width(), A.Height() );
    Transpose( AFilt.LockedMatrix(), this->Matrix(), conjugate );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::TransposeRowFilterFrom
( const DistMatrix<T,VGath,U>& A, bool conjugate )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::TransposeRowFilterFrom"))
    DistMatrix<T,V,U> AFilt( A.Grid() );
    if( this->ColConstrained() )
        AFilt.AlignRowsWith( *this, false );
    if( this->RowConstrained() )
        AFilt.AlignColsWith( *this, false );
    copy::ColFilter( A, AFilt );
    if( !this->ColConstrained() )
        this->AlignColsWith( AFilt, false );
    if( !this->RowConstrained() )
        this->AlignRowsWith( AFilt, false );
    this->Resize( A.Width(), A.Height() );
    Transpose( AFilt.LockedMatrix(), this->Matrix(), conjugate );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::TransposePartialColFilterFrom
( const DistMatrix<T,V,UPart>& A, bool conjugate )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::TransposePartialColFilterFrom"))
    DistMatrix<T,V,U> AFilt( A.Grid() );
    if( this->ColConstrained() )
        AFilt.AlignRowsWith( *this, false );
    if( this->RowConstrained() )
        AFilt.AlignColsWith( *this, false );
    copy::PartialRowFilter( A, AFilt );
    if( !this->ColConstrained() )
        this->AlignColsWith( AFilt, false );
    if( !this->RowConstrained() )
        this->AlignRowsWith( AFilt, false );
    this->Resize( A.Width(), A.Height() );
    Transpose( AFilt.LockedMatrix(), this->Matrix(), conjugate );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::TransposePartialRowFilterFrom
( const DistMatrix<T,VPart,U>& A, bool conjugate )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::TransposePartialRowFilterFrom"))
    DistMatrix<T,V,U> AFilt( A.Grid() );
    if( this->ColConstrained() )
        AFilt.AlignRowsWith( *this, false );
    if( this->RowConstrained() )
        AFilt.AlignColsWith( *this, false );
    copy::PartialColFilter( A, AFilt );
    if( !this->ColConstrained() )
        this->AlignColsWith( AFilt, false );
    if( !this->RowConstrained() )
        this->AlignRowsWith( AFilt, false );
    this->Resize( A.Width(), A.Height() );
    Transpose( AFilt.LockedMatrix(), this->Matrix(), conjugate );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::AdjointColFilterFrom( const DistMatrix<T,V,UGath>& A )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::AdjointColFilterFrom"))
    this->TransposeColFilterFrom( A, true );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::AdjointRowFilterFrom( const DistMatrix<T,VGath,U>& A )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::AdjointRowFilterFrom"))
    this->TransposeRowFilterFrom( A, true );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::AdjointPartialColFilterFrom
( const DistMatrix<T,V,UPart>& A )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::AdjointPartialColFilterFrom"))
    this->TransposePartialColFilterFrom( A, true );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::AdjointPartialRowFilterFrom
( const DistMatrix<T,VPart,U>& A )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::AdjointPartialRowFilterFrom"))
    this->TransposePartialRowFilterFrom( A, true );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::TransposeColSumScatterFrom
( const DistMatrix<T,V,UGath>& A, bool conjugate )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::TransposeColSumScatterFrom"))
    DistMatrix<T,V,U> ASumFilt( A.Grid() );
    if( this->ColConstrained() )
        ASumFilt.AlignRowsWith( *this, false );
    if( this->RowConstrained() )
        ASumFilt.AlignColsWith( *this, false );
    copy::RowSumScatter( A, ASumFilt );
    if( !this->ColConstrained() )
        this->AlignColsWith( ASumFilt, false );
    if( !this->RowConstrained() )
        this->AlignRowsWith( ASumFilt, false );
    this->Resize( A.Width(), A.Height() );
    Transpose( ASumFilt.LockedMatrix(), this->Matrix(), conjugate );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::TransposePartialColSumScatterFrom
( const DistMatrix<T,V,UPart>& A, bool conjugate )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::TransposePartialColSumScatterFrom"))
    DistMatrix<T,V,U> ASumFilt( A.Grid() );
    if( this->ColConstrained() )
        ASumFilt.AlignRowsWith( *this, false );
    if( this->RowConstrained() )
        ASumFilt.AlignColsWith( *this, false );
    ASumFilt.PartialRowSumScatterFrom( A );
    if( !this->ColConstrained() )
        this->AlignColsWith( ASumFilt, false );
    if( !this->RowConstrained() )
        this->AlignRowsWith( ASumFilt, false );
    this->Resize( A.Width(), A.Height() );
    Transpose( ASumFilt.LockedMatrix(), this->Matrix(), conjugate );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::AdjointColSumScatterFrom
( const DistMatrix<T,V,UGath>& A )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::AdjointColSumScatterFrom"))
    this->TransposeColSumScatterFrom( A, true );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::AdjointPartialColSumScatterFrom
( const DistMatrix<T,V,UPart>& A )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::AdjointPartialColSumScatterFrom"))
    this->TransposePartialColSumScatterFrom( A, true );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::TransposeColSumScatterUpdate
( T alpha, const DistMatrix<T,V,UGath>& A, bool conjugate )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::TransposeColSumScatterUpdate"))
    DistMatrix<T,V,U> ASumFilt( A.Grid() );
    if( this->ColConstrained() )
        ASumFilt.AlignRowsWith( *this, false );
    if( this->RowConstrained() )
        ASumFilt.AlignColsWith( *this, false );
    copy::RowSumScatter( A, ASumFilt );
    if( !this->ColConstrained() )
        this->AlignColsWith( ASumFilt, false );
    if( !this->RowConstrained() )
        this->AlignRowsWith( ASumFilt, false );
    // ALoc += alpha ASumFiltLoc'
    El::Matrix<T>& ALoc = this->Matrix();
    const El::Matrix<T>& BLoc = ASumFilt.LockedMatrix();
    const Int localHeight = ALoc.Height();
    const Int localWidth = ALoc.Width();
    if( conjugate )
    {
        for( Int jLoc=0; jLoc<localWidth; ++jLoc )
            for( Int iLoc=0; iLoc<localHeight; ++iLoc )
                ALoc.Update( iLoc, jLoc, alpha*Conj(BLoc.Get(jLoc,iLoc)) );
    }
    else
    {
        for( Int jLoc=0; jLoc<localWidth; ++jLoc )
            for( Int iLoc=0; iLoc<localHeight; ++iLoc )
                ALoc.Update( iLoc, jLoc, alpha*BLoc.Get(jLoc,iLoc) );
    }
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::TransposePartialColSumScatterUpdate
( T alpha, const DistMatrix<T,V,UPart>& A, bool conjugate )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::TransposePartialColSumScatterUpdate"))
    DistMatrix<T,V,U> ASumFilt( A.Grid() );
    if( this->ColConstrained() )
        ASumFilt.AlignRowsWith( *this, false );
    if( this->RowConstrained() )
        ASumFilt.AlignColsWith( *this, false );
    ASumFilt.PartialRowSumScatterFrom( A );
    if( !this->ColConstrained() )
        this->AlignColsWith( ASumFilt, false );
    if( !this->RowConstrained() )
        this->AlignRowsWith( ASumFilt, false );
    // ALoc += alpha ASumFiltLoc'
    El::Matrix<T>& ALoc = this->Matrix();
    const El::Matrix<T>& BLoc = ASumFilt.LockedMatrix();
    const Int localHeight = ALoc.Height();
    const Int localWidth = ALoc.Width();
    if( conjugate )
    {
        for( Int jLoc=0; jLoc<localWidth; ++jLoc )
            for( Int iLoc=0; iLoc<localHeight; ++iLoc )
                ALoc.Update( iLoc, jLoc, alpha*Conj(BLoc.Get(jLoc,iLoc)) );
    }
    else
    {
        for( Int jLoc=0; jLoc<localWidth; ++jLoc )
            for( Int iLoc=0; iLoc<localHeight; ++iLoc )
                ALoc.Update( iLoc, jLoc, alpha*BLoc.Get(jLoc,iLoc) );
    }
}

template<typename T,Dist U,Dist V>
void GeneralDistMatrix<T,U,V>::AdjointColSumScatterUpdate
( T alpha, const DistMatrix<T,V,UGath>& A )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::AdjointColSumScatterUpdate"))
    this->TransposeColSumScatterUpdate( alpha, A, true );
}

template<typename T,Dist U,Dist V>
void GeneralDistMatrix<T,U,V>::AdjointPartialColSumScatterUpdate
( T alpha, const DistMatrix<T,V,UPart>& A )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::AdjointPartialColSumScatterUpdate"))
    this->TransposePartialColSumScatterUpdate( alpha, A, true );
}

// Basic queries
// =============
// Distribution information
// ------------------------
template<typename T,Dist U,Dist V>
Dist GeneralDistMatrix<T,U,V>::ColDist() const { return U; }
template<typename T,Dist U,Dist V>
Dist GeneralDistMatrix<T,U,V>::RowDist() const { return V; }
template<typename T,Dist U,Dist V>
Dist GeneralDistMatrix<T,U,V>::PartialColDist() const 
{ return Partial<U>(); }
template<typename T,Dist U,Dist V>
Dist GeneralDistMatrix<T,U,V>::PartialRowDist() const 
{ return Partial<V>(); }
template<typename T,Dist U,Dist V>
Dist GeneralDistMatrix<T,U,V>::PartialUnionColDist() const 
{ return PartialUnionCol<U,V>(); }
template<typename T,Dist U,Dist V>
Dist GeneralDistMatrix<T,U,V>::PartialUnionRowDist() const 
{ return PartialUnionRow<U,V>(); }

// Diagonal manipulation
// =====================
template<typename T,Dist U,Dist V>
void GeneralDistMatrix<T,U,V>::GetDiagonal
( AbstractDistMatrix<T>& d, Int offset ) const
{
    DEBUG_ONLY(CallStackEntry cse("GDM::GetDiagonal"))
    this->GetDiagonalHelper
    ( d, offset, []( T& alpha, T beta ) { alpha = beta; } );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::GetRealPartOfDiagonal
( AbstractDistMatrix<Base<T>>& d, Int offset ) const
{
    DEBUG_ONLY(CallStackEntry cse("GDM::GetRealPartOfDiagonal"))
    this->GetDiagonalHelper
    ( d, offset, []( Base<T>& alpha, T beta ) { alpha = RealPart(beta); } );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::GetImagPartOfDiagonal
( AbstractDistMatrix<Base<T>>& d, Int offset ) const
{
    DEBUG_ONLY(CallStackEntry cse("GDM::GetImagPartOfDiagonal"))
    this->GetDiagonalHelper
    ( d, offset, []( Base<T>& alpha, T beta ) { alpha = ImagPart(beta); } );
}

template<typename T,Dist U,Dist V>
auto
GeneralDistMatrix<T,U,V>::GetDiagonal( Int offset ) const
-> DistMatrix<T,UDiag,VDiag>
{
    DistMatrix<T,UDiag,VDiag> d( this->Grid() );
    GetDiagonal( d, offset );
    return d;
}

template<typename T,Dist U,Dist V>
auto
GeneralDistMatrix<T,U,V>::GetRealPartOfDiagonal( Int offset ) const
-> DistMatrix<Base<T>,UDiag,VDiag>
{
    DistMatrix<Base<T>,UDiag,VDiag> d( this->Grid() );
    GetRealPartOfDiagonal( d, offset );
    return d;
}

template<typename T,Dist U,Dist V>
auto
GeneralDistMatrix<T,U,V>::GetImagPartOfDiagonal( Int offset ) const
-> DistMatrix<Base<T>,UDiag,VDiag>
{
    DistMatrix<Base<T>,UDiag,VDiag> d( this->Grid() );
    GetImagPartOfDiagonal( d, offset );
    return d;
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::SetDiagonal
( const AbstractDistMatrix<T>& d, Int offset )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::SetDiagonal"))
    this->SetDiagonalHelper
    ( d, offset, []( T& alpha, T beta ) { alpha = beta; } );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::SetRealPartOfDiagonal
( const AbstractDistMatrix<Base<T>>& d, Int offset )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::SetRealPartOfDiagonal"))
    this->SetDiagonalHelper
    ( d, offset, 
      []( T& alpha, Base<T> beta ) { El::SetRealPart(alpha,beta); } );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::SetImagPartOfDiagonal
( const AbstractDistMatrix<Base<T>>& d, Int offset )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::SetImagPartOfDiagonal"))
    this->SetDiagonalHelper
    ( d, offset, 
      []( T& alpha, Base<T> beta ) { El::SetImagPart(alpha,beta); } );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::UpdateDiagonal
( T gamma, const AbstractDistMatrix<T>& d, Int offset )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::UpdateDiagonal"))
    this->SetDiagonalHelper
    ( d, offset, [gamma]( T& alpha, T beta ) { alpha += gamma*beta; } );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::UpdateRealPartOfDiagonal
( Base<T> gamma, const AbstractDistMatrix<Base<T>>& d, Int offset )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::UpdateRealPartOfDiagonal"))
    this->SetDiagonalHelper
    ( d, offset, 
      [gamma]( T& alpha, Base<T> beta ) 
      { El::UpdateRealPart(alpha,gamma*beta); } );
}

template<typename T,Dist U,Dist V>
void
GeneralDistMatrix<T,U,V>::UpdateImagPartOfDiagonal
( Base<T> gamma, const AbstractDistMatrix<Base<T>>& d, Int offset )
{
    DEBUG_ONLY(CallStackEntry cse("GDM::UpdateImagPartOfDiagonal"))
    this->SetDiagonalHelper
    ( d, offset, 
      [gamma]( T& alpha, Base<T> beta ) 
      { El::UpdateImagPart(alpha,gamma*beta); } );
}

// Private section
// ###############

// Diagonal helper functions
// =========================
template<typename T,Dist U,Dist V>
template<typename S,class Function>
void
GeneralDistMatrix<T,U,V>::GetDiagonalHelper
( AbstractDistMatrix<S>& dPre, Int offset, Function func ) const
{
    DEBUG_ONLY(
      CallStackEntry cse("GDM::GetDiagonalHelper");
      AssertSameGrids( *this, dPre );
    )
    ProxyCtrl ctrl;
    ctrl.colConstrain = true;
    ctrl.colAlign = this->DiagonalAlign(offset);
    ctrl.rootConstrain = true;
    ctrl.root = this->DiagonalRoot(offset);
    auto dPtr = WriteProxy<S,UDiag,VDiag>(&dPre,ctrl);
    auto& d = *dPtr;

    d.Resize( this->DiagonalLength(offset), 1 );
    if( !d.Participating() )
        return;

    const Int diagShift = d.ColShift();
    const Int iStart = diagShift + Max(-offset,0);
    const Int jStart = diagShift + Max( offset,0);

    const Int colStride = this->ColStride();
    const Int rowStride = this->RowStride();
    const Int iLocStart = (iStart-this->ColShift()) / colStride;
    const Int jLocStart = (jStart-this->RowShift()) / rowStride;
    const Int iLocStride = d.ColStride() / colStride;
    const Int jLocStride = d.ColStride() / rowStride;

    const Int localDiagLength = d.LocalHeight();
    S* dBuf = d.Buffer();
    const T* buffer = this->LockedBuffer();
    const Int ldim = this->LDim();
    EL_PARALLEL_FOR
    for( Int k=0; k<localDiagLength; ++k )
    {
        const Int iLoc = iLocStart + k*iLocStride;
        const Int jLoc = jLocStart + k*jLocStride;
        func( dBuf[k], buffer[iLoc+jLoc*ldim] );
    }
}

template<typename T,Dist U,Dist V>
template<typename S,class Function>
void
GeneralDistMatrix<T,U,V>::SetDiagonalHelper
( const AbstractDistMatrix<S>& dPre, Int offset, Function func ) 
{
    DEBUG_ONLY(
      CallStackEntry cse("GDM::SetDiagonalHelper");
      AssertSameGrids( *this, dPre );
    )
    ProxyCtrl ctrl;
    ctrl.colConstrain = true;
    ctrl.colAlign = this->DiagonalAlign(offset);
    ctrl.rootConstrain = true;
    ctrl.root = this->DiagonalRoot(offset);
    auto dPtr = ReadProxy<S,UDiag,VDiag>(&dPre,ctrl); 
    const auto& d = *dPtr;

    if( !d.Participating() )
        return;

    const Int diagShift = d.ColShift();
    const Int iStart = diagShift + Max(-offset,0);
    const Int jStart = diagShift + Max( offset,0);

    const Int colStride = this->ColStride();
    const Int rowStride = this->RowStride();
    const Int iLocStart = (iStart-this->ColShift()) / colStride;
    const Int jLocStart = (jStart-this->RowShift()) / rowStride;
    const Int iLocStride = d.ColStride() / colStride;
    const Int jLocStride = d.ColStride() / rowStride;

    const Int localDiagLength = d.LocalHeight();
    const S* dBuf = d.LockedBuffer();
    T* buffer = this->Buffer();
    const Int ldim = this->LDim();
    EL_PARALLEL_FOR
    for( Int k=0; k<localDiagLength; ++k )
    {
        const Int iLoc = iLocStart + k*iLocStride;
        const Int jLoc = jLocStart + k*jLocStride;
        func( buffer[iLoc+jLoc*ldim], dBuf[k] );
    }
}

// Instantiations for {Int,Real,Complex<Real>} for each Real in {float,double}
// ###########################################################################

#define DISTPROTO(T,U,V) template class GeneralDistMatrix<T,U,V>
  
#define PROTO(T)\
  DISTPROTO(T,CIRC,CIRC);\
  DISTPROTO(T,MC,  MR  );\
  DISTPROTO(T,MC,  STAR);\
  DISTPROTO(T,MD,  STAR);\
  DISTPROTO(T,MR,  MC  );\
  DISTPROTO(T,MR,  STAR);\
  DISTPROTO(T,STAR,MC  );\
  DISTPROTO(T,STAR,MD  );\
  DISTPROTO(T,STAR,MR  );\
  DISTPROTO(T,STAR,STAR);\
  DISTPROTO(T,STAR,VC  );\
  DISTPROTO(T,STAR,VR  );\
  DISTPROTO(T,VC,  STAR);\
  DISTPROTO(T,VR,  STAR);

#include "El/macros/Instantiate.h"

} // namespace El
