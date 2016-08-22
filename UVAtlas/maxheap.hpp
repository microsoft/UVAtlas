//-------------------------------------------------------------------------------------
// UVAtlas - maxheap.hpp
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
    enum MANAGEMODE
    {
        AUTOMATIC,
        MANUAL,
    };
    
    const size_t NOT_IN_HEAP = 0xffffffff;
    typedef size_t pos_type;

    template <class _Ty1, class _Ty2>
    class CMaxHeap;

    template <class _Ty1, class _Ty2>
    class CMaxHeapItem
    {	
        friend class CMaxHeap<_Ty1, _Ty2>;
        
        public:
            // _Ty1 should be a simple type so that could be compare use < > ==...
            typedef _Ty1 weight_type; 
            typedef _Ty2 data_type;			

            weight_type m_weight;
            data_type m_data;
        private:		
            pos_type m_position;

        public:
            CMaxHeapItem()
                :m_weight(0)
                ,m_position(NOT_IN_HEAP)
            { 
            }
            
            CMaxHeapItem(weight_type weight, data_type data)
                :m_weight(weight)
                ,m_position(NOT_IN_HEAP)
                ,m_data(data)
            { 		
            }
            
            CMaxHeapItem(const CMaxHeapItem& item)
            {
                m_weight = item.m_weight;
                m_position = item.m_position;
                m_data = item.m_data; //Shallow copy here! 
            }

            pos_type getPos()
            {
                return m_position;
            }			

            bool isItemInHeap()
            {
                return m_position != NOT_IN_HEAP;
            }
    };

    template <class _Ty1, class _Ty2>
    class CMaxHeap
    {
        public:
            typedef _Ty1 weight_type;
            typedef _Ty2 data_type;					
            typedef CMaxHeapItem<weight_type, data_type> item_type;

            CMaxHeap():m_size(0), m_bAutoMangeMemory(false)
            {		
            }
            CMaxHeap(size_t size):m_size(0), m_bAutoMangeMemory(false)
            {
                m_items.reserve(size);			
            }
            
            ~CMaxHeap()
            {
                if (m_bAutoMangeMemory)
                {
                    while(m_size > 0)
                    {
                        cutTopData();
                    }
                }
            }

            bool resize(size_t newsize)
            {
                try
                {
                    m_items.resize(newsize);
                    return true;
                }
                catch (std::bad_alloc&)
                {
                    return false;
                }
            }
            
            bool insert(item_type* pItem)
            { 
                if ( !pItem )
                {
                    return false;
                }

                if ( m_items.size() > m_size)
                {
                    m_items[m_size] = pItem;
                }
                else
                {
                    try
                    {
                        m_items.push_back(pItem);
                    }
                    catch (std::bad_alloc&)
                    {
                        return false;
                    }
                }
                            
                pItem->m_position = m_size;
                m_size++;

                upheap(pItem->m_position);
                return true;
            };

            bool insertData(data_type data, weight_type weight)
            {
                if (!m_bAutoMangeMemory)
                {
                    return false;
                }
                item_type* pNewItem = new (std::nothrow) item_type;
                if (!pNewItem)
                {
                    return false;
                }

                pNewItem->m_data = data;
                pNewItem->m_weight = weight;
                return insert(pNewItem);
            }

            data_type cutTopData()
            {
                item_type* pTop = removeAt(0);
                if (!pTop)
                {
                    return 0;
                }
                
                data_type data = pTop->m_data;
                delete pTop;
                return data;
            }

            void update(item_type* pItem, weight_type newweight)
            {
                if (!pItem)
                {
                    return;
                }
                pos_type i = pItem->m_position;
                if (i>=m_size || i == NOT_IN_HEAP)
                {
                    return;
                }
                weight_type oldweight = pItem->m_weight;
                pItem->m_weight = newweight;
                if (newweight<oldweight)
                {
                    downheap(i);
                }
                else
                {
                    upheap(i);
                }
            }

            item_type* cutTop()
            {
                return removeAt(0);
            }
            item_type* remove(item_type* pItem)
            {				
                pos_type i = pItem->getPos();

                return removeAt(i);
            }	
            
            size_t size()
            {
                return m_size;
            }

            bool empty()
            {
                return (m_size == 0);
            }

            void SetManageMode(MANAGEMODE mode)
            { 
                if (mode == AUTOMATIC)
                {
                    m_bAutoMangeMemory = true; 
                }
                else
                {
                    m_bAutoMangeMemory = false; 
                }
            }
        private:

            item_type* removeAt(pos_type i)
            {								
                if (m_size == 0 || i >= m_size)
                {
                    return nullptr;
                }

                swapnode(i, m_size-1);
                m_size--;
                (m_items[m_size])->m_position = NOT_IN_HEAP;
                
                if ((m_items[i])->m_weight < (m_items[m_size])->m_weight )
                {
                    downheap(i);
                }
                else
                {
                    upheap(i);
                }

                return m_items[m_size];
            }	
                        
            pos_type parent(pos_type i)
            {
                return (i-1)>>1;
            }
            
            pos_type leftChild(pos_type i)
            {
                return (i<<1)+1;
            }

            pos_type rightChild(pos_type i)
            {
                return (i<<1)+2;
            }

            void swapnode(size_t i, size_t j)
            {
                if ( i == j)
                    return;
                
                std::swap(m_items[i], m_items[j]);
                (m_items[i])->m_position = i;
                (m_items[j])->m_position = j;
            }
            
            void downheap(pos_type i)
            {
                while (i<m_size)
                {
                    pos_type larger = i;

                    pos_type left = leftChild(i);
                    pos_type right = rightChild(i);

                    weight_type maxweight = (m_items[i])->m_weight;
                    
                    if (left < m_size &&(m_items[left])->m_weight > maxweight)
                    {
                        larger = left;
                        maxweight = (m_items[left])->m_weight;
                    }
                    if (right < m_size &&(m_items[right])->m_weight > maxweight)

                    {
                        larger = right;
                    }
                    
                    if (larger != i)
                    {
                        swapnode(i, larger);						
                        i = larger;
                    }
                    else
                    {
                        break;
                    }
                }
            }

            void upheap(pos_type i)
            {
                while (i >0)
                {
                    pos_type parentPos = parent(i);
                    if ((m_items[i])->m_weight >  (m_items[parentPos])->m_weight)
                    {
                        swapnode(i, parentPos);					
                        i = parentPos;
                    }
                    else
                    {
                        break;
                    }
                }
            }

        private:
            std::vector< item_type* > m_items;
            bool m_bAutoMangeMemory;
            size_t m_size;
    };
}
