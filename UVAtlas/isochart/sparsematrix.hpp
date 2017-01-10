//-------------------------------------------------------------------------------------
// UVAtlas - sparsematrix.hpp
//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//  
// Copyright (c) Microsoft Corporation. All rights reserved.
//
// http://go.microsoft.com/fwlink/?LinkID=512686
//-------------------------------------------------------------------------------------

#pragma once

namespace Isochart
{
    template<class TYPE>
    class CVector : public std::vector<TYPE>
    {
    public:
        typedef size_t size_type;
        typedef TYPE value_type;
    
        public:
            void setZero()
            {
                memset(this->data(), 0, this->size()*sizeof(value_type));
            }

        public:
            template <class T>
            static T dot(
                const CVector<T>& v1,
                const CVector<T>& v2)
            {
                assert(v1.size() == v2.size());

                T result = 0;
                for (size_type ii=0; ii<v1.size(); ii++)
                {
                    result += v1[ii]*v2[ii];
                }
                return result;
            }

            template <class T>
            static bool subtract(
                CVector<T>& destVec,
                const CVector<T>& v1,
                const CVector<T>& v2)
            {
                assert(v1.size() == v2.size());

                try
                {
                    destVec.resize(v1.size());
                }
                catch (std::bad_alloc&)
                {
                    return false;
                }
                for (size_type ii=0; ii<destVec.size(); ii++)
                {
                    destVec[ii] = v1[ii] - v2[ii];
                }
                return true;
            }

            template <class T>
            static bool addTogether(
                CVector<T>& destVec,
                const CVector<T>& v1,
                const CVector<T>& v2)
            {
                assert(v1.size() == v2.size());

                try
                {
                    destVec.resize(v1.size());
                }
                catch (std::bad_alloc&)
                {
                    return false;
                }
                for (size_type ii=0; ii<destVec.size(); ii++)
                {
                    destVec[ii] = v1[ii] + v2[ii];
                }
                return true;
            }

            template <class T>
            static bool assign(CVector<T>& dest, const CVector<T>& src)
            {
                try
                {
                    dest.resize(src.size());
                }
                catch(std::bad_alloc&)
                {
                    return false;
                }
                memcpy(dest.data(), src.data(), src.size()*sizeof(T));
                return true;
            }

            template <class T>
            static bool scale(
                CVector<T>& dest, const CVector<T>& src, T scaleFactor)
            {
                try
                {
                    dest.resize(src.size());
                }
                catch (std::bad_alloc&)
                {
                    return false;
                }
                for (size_type ii=0; ii<src.size(); ii++)
                {
                    dest[ii] = src[ii]*scaleFactor;
                }
                return true;
            }

            template <class T>
            static T length(
                const CVector<T>& v1)
            {				
                T result = dot<T>(v1, v1);
                return result<0?0:static_cast<T>(sqrt(result));
            }
            
    };

    template<class TYPE>
    class CSparseMatrix
    {
    public:
        typedef size_t size_type;
        typedef size_t pos_type;
        typedef TYPE value_type;

        static const size_type NOT_IN_MATRIX = 0xffffffff;
        class RowItem
        {
            public:
                pos_type colIdx;
                value_type value;
            public:
                RowItem()
                {
                    colIdx = NOT_IN_MATRIX;
                    value = 0;
                }
                RowItem(pos_type _colIdx, value_type _value)
                {
                    colIdx = _colIdx;
                    value = _value;
                }
        };

        class Row
        {
            private:
                std::vector<RowItem> m_items;
            public:
                void Clear() {m_items.clear();}
                
                size_type size() const {return m_items.size();}
                
                RowItem& operator [] (const pos_type col) {return m_items[col];}
                const RowItem& operator [] (const pos_type col)const {return m_items[col];}

                size_type insert(const pos_type _col, const value_type _value)
                {
                    size_type idx = index(_col);
                    if (idx == NOT_IN_MATRIX)
                    {
                        idx = m_items.size();
                        RowItem item(_col, _value);

                        try
                        {
                            m_items.push_back(item);
                        }
                        catch (std::bad_alloc&)
                        {
                            return NOT_IN_MATRIX;
                        }
                    }
                    else
                    {
                        RowItem& item = m_items[idx];
                        item.value = _value;
                    }
                    return idx;
                }

                bool increase(const pos_type _col, const value_type delta)
                {
                    size_type idx = index(_col);
                    if (idx == NOT_IN_MATRIX)
                    {
                        try
                        {
                            RowItem item(_col, delta);
                            m_items.push_back(item);
                        }
                        catch(std::bad_alloc&)
                        {
                            return false;
                        }
                    }
                    else
                    {
                        RowItem& item = m_items[idx];
                        item.value += delta;
                    }
                    return true;
                }				

                value_type getCol(const pos_type _col)
                {
                    size_type idx = index(_col);
                    if (idx == NOT_IN_MATRIX)
                    {
                        return 0;
                    }
                    else
                    {
                        RowItem& item = m_items[idx];
                        return item.value;
                    }
                }

            private:
                size_type index(const pos_type _colIdx)
                {
                    for (size_type i=0; i<size(); i++)
                    {
                        if (m_items[i].colIdx == _colIdx)
                        {
                            return i;
                        }
                    }
                    return NOT_IN_MATRIX;
                }

        };

        public:
            CSparseMatrix() {m_colCount = 0;}
            size_type rowCount() const { return m_rows.size(); }
            size_type colCount() const { return m_colCount; }
            bool resize(size_type _rowCount, size_type _colCount)
            {
                if (_rowCount == rowCount())
                {
                    m_colCount = _colCount;
                    return true;
                }

                try
                {
                    m_rows.resize(_rowCount);
                }
                catch (std::bad_alloc&)
                {
                    return false;
                }
                m_colCount = _colCount;
                return true;
            }
            
            Row& getRow(pos_type rowIdx)
            {
                return m_rows[rowIdx];
            }

            const Row& getRow (pos_type rowIdx) const
            {
                return m_rows[rowIdx];
            }
            
            bool setItem(pos_type rowIdx, pos_type colIdx, value_type value)
            {
                assert(rowIdx < rowCount() && colIdx < colCount());
                Row& row = getRow(rowIdx);
                if (row.insert(colIdx, value) == NOT_IN_MATRIX)
                {
                    return false;
                }
                return true;
            }

            value_type getItem(pos_type rowIdx, pos_type colIdx)
            {
                assert(rowIdx < rowCount() && colIdx < colCount());
                Row& row = getRow(rowIdx);
                return row.getCol(colIdx);
            }


            bool increase(pos_type rowIdx, pos_type colIdx, value_type delta)
            {
                assert(rowIdx < rowCount() && colIdx < colCount());
                Row& row = getRow(rowIdx);
                if (!row.increase(colIdx, delta))
                {
                    return false;
                }
                return true;

            }
        private:
            std::vector<Row> m_rows;
            size_type m_colCount;

        public:
        // v' = A * v
        template<class T>
        static bool Mat_Mul_Vec(
            CVector<T>& destVec, 
            const CSparseMatrix<T>& srcMat, 
            const CVector<T>& srcVec)
        {
            assert(srcMat.colCount() == srcVec.size());

            try
            {
                destVec.resize(srcMat.rowCount());
            }
            catch (std::bad_alloc&)
            {
                return false;
            }
            
            for (size_type ii=0; ii<srcMat.rowCount(); ii++)
            {
                destVec[ii] = 0;

                const Row& row = srcMat.getRow(ii);

                for (size_type jj=0; jj<row.size(); jj++)
                {
                    const RowItem& item = row[jj];
                    assert(item.colIdx < srcMat.colCount());
                    destVec[ii] += item.value * srcVec[item.colIdx];
                }
            }
            return true;
        }

        // v' = A^T * v
        template<class T>
        static bool Mat_Trans_Mul_Vec(
            CVector<T>& destVec, 
            const CSparseMatrix<T>& srcMat, 
            const CVector<T>& srcVec)
        {
            assert(srcMat.rowCount() == srcVec.size());

            try
            {
                destVec.resize(srcMat.colCount());
            }
            catch (std::bad_alloc&)
            {
                return false;
            }

            destVec.setZero();
            for (size_type ii=0; ii<srcMat.rowCount(); ii++)
            {
                const Row& row = srcMat.getRow(ii);

                for (size_type jj=0; jj<row.size(); jj++)
                {
                    const RowItem& item = row[jj];
                    assert(item.colIdx < srcMat.colCount());
                    destVec[item.colIdx] += item.value * srcVec[ii];
                }
            }
            return true;
        }

        // A' = A^T * A
        template<class T>
        static bool Mat_Trans_MUL_Mat(
            CSparseMatrix<T>&  destMat, 
            const CSparseMatrix<T>& srcMat)
        {
            if (!destMat.resize(srcMat.colCount(), srcMat.colCount()))
            {
                return false;
            }

            for (size_type ii=0; ii<srcMat.rowCount(); ii++)
            {
                const Row& row = srcMat.getRow(ii);

                for (size_type jj=0; jj<row.size(); jj++)
                {
                    const RowItem& item1 = row[jj];
                    for (size_type kk=0; kk<row.size(); kk++)
                    {
                        const RowItem& item2 = row[kk];
                        if (!destMat.increase(
                            item1.colIdx,
                            item2.colIdx,
                            item1.value * item2.value))
                        {
                            return false;
                        }
                    }
                }
            }
        
            return true;
        }

        // v' = (A^T*A)*v
        template<class T>
        static bool Mat_SYMM_MUL_Vec(
            CVector<T>& destVec, 
            const CSparseMatrix<T>& srcMat, 
            const CVector<T>& srcVec)
        {
            assert(srcMat.colCount() == srcVec.size());

            if (!destVec.resize(srcMat.colCount()))
            {
                return false;
            }

            CVector<T> tempVec;
            if (!Mat_Mul_Vec<T>(tempVec, srcMat, srcVec))
            {
                return false;
            }

            if (!Mat_Trans_Mul_Vec<T>(destVec, srcMat, tempVec))
            {
                return false;
            }
            
            return true;
        }

        template<class T>
        static bool ConjugateGradient(
            CVector<T>& X,
            const CSparseMatrix<T>& A,
            const CVector<T>& B,			
            size_type maxIteration,			
            T epsilon,
            size_type& iter)
        {	
            if (X.size() != A.colCount())
            {
                try
                {
                    X.resize(A.colCount());
                }
                catch (std::bad_alloc&)
                {
                    return false;
                }
                X.setZero();
            }            

            CVector<T> R, D, Q, tempV;

            if (!Mat_Mul_Vec(R, A, X))
            {
                return false;
            }
            if (!CVector<T>::subtract(R, B, R))
            {
                return false;
            }
            if (!CVector<T>::assign(D, R))
            {
                return false;
            }

            T deltaNew = CVector<T>::dot(R, R);
            T deltaZero = deltaNew;
            T deltaOld = 0;

            T errBound = deltaZero*epsilon*epsilon;

            iter = 0;
            while(iter < maxIteration && deltaNew > errBound)
            {
                if (!Mat_Mul_Vec(Q, A, D))
                {
                    return false;
                }

                T a = deltaNew / CVector<T>::dot(D, Q);

                if (!CVector<T>::scale(tempV, D, a))
                {
                    return false;
                }
                if (!CVector<T>::addTogether(X, X, tempV))
                {
                    return false;	
                }

                if (iter%10 == 0)
                {
                    if (!Mat_Mul_Vec(R, A, X))
                    {
                        return false;
                    }
                    if (!CVector<T>::subtract(R, B, R))
                    {
                        return false;
                    }
                }
                else
                {
                    if (!CVector<T>::scale(tempV, Q, a))
                    {
                        return false;
                    }
                    if (!CVector<T>::subtract(R, R, tempV))
                    {
                        return false;
                    }
                }
                deltaOld = deltaNew;

                deltaNew = CVector<T>::dot(R, R);

                T b = deltaNew / deltaOld;
                
                if (!CVector<T>::scale(tempV, D, b))
                {
                    return false;
                }
                if (!CVector<T>::addTogether(D, R, tempV))
                {
                    return false;
                }

                iter++;
            }
            return true;
        }
        
    };
}

