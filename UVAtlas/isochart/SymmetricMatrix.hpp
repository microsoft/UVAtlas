//-------------------------------------------------------------------------------------
// UVAtlas - SymmetricMatrix.hpp
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkID=512686
//-------------------------------------------------------------------------------------

#pragma once

namespace Isochart
{
#ifdef UVATLAS_USE_EIGEN

    template<class TYPE>
    class CSymmetricMatrix
    {
    public:
        using value_type = TYPE;

        _Success_(return)
            static bool
            GetEigen(
                size_t dwDimension,
                _In_reads_(dwDimension* dwDimension) const value_type* pMatrix,
                _Out_writes_(dwMaxRange) value_type* pEigenValue,
                _Out_writes_(dwDimension* dwMaxRange) value_type* pEigenVector,
                size_t dwMaxRange,
                float epsilon = 1e-10f)
        {
            // Check arguments.
            if (!pMatrix || !pEigenValue || !pEigenVector)
                return false;

            if (dwDimension < dwMaxRange
                || dwMaxRange == 0
                || dwDimension == 0)
            {
                return false;
            }

            using EigenMatrix = Eigen::Matrix<value_type, Eigen::Dynamic, Eigen::Dynamic>;

            Eigen::Map<const EigenMatrix> matrix(pMatrix, static_cast<long>(dwDimension), static_cast<long>(dwDimension));
            Eigen::Map<EigenMatrix> eigenvalues(pEigenValue, static_cast<long>(dwMaxRange), 1);
            Eigen::Map<EigenMatrix> eigenvectors(pEigenVector, static_cast<long>(dwDimension), static_cast<long>(dwMaxRange));

            // If we don't want every eigenvalue, try solving with Spectra first.
            if (dwMaxRange < dwDimension)
            {
                try
                {
                    constexpr int maxIterations = 1000; // Spectra's default

                    // Construct matrix operation object using the wrapper class DenseSymMatProd.
                    Spectra::DenseSymMatProd<value_type> op(matrix);
                    // Construct eigen solver object, requesting the largest dwMaxRange eigenvalues
                    Spectra::SymEigsSolver< Spectra::DenseSymMatProd<value_type> > eigs(
                        op,
                        static_cast<int>(dwMaxRange),
                        // Convergence speed, higher is faster with more memory usage, recommended to be at least 2x nev, must be <= dimension.
                        static_cast<int>(std::min(dwMaxRange * 2, dwDimension))
                    );
                    eigs.init();
                    auto const numConverged = eigs.compute(
                        Spectra::SortRule::LargestAlge,  // Sort by descending eigenvalues.
                        maxIterations,
                        epsilon
                    );

                    if (numConverged >= static_cast<int>(dwMaxRange) && eigs.info() == Spectra::CompInfo::Successful)
                    {
                        eigenvalues = eigs.eigenvalues();
                        eigenvectors = eigs.eigenvectors();
                        return true;
                    }
                    else
                    {
                        DPF(0, "Spectra::SymEigsSolver failed with info() == %d, numConverged == %d, dwDimension == %d, dwMaxRange == %d", eigs.info(), numConverged, dwDimension, dwMaxRange);
                    }
                }
                catch (const std::exception& ex)
                {
                    DPF(0, "Spectra::SymEigsSolver threw an exception with what() == \"%s\", dwDimension == %d, dwMaxRange == %d", ex.what(), dwDimension, dwMaxRange);
                }
            }

            // Otherwise, fallback to Eigen built-in solver.
            const Eigen::SelfAdjointEigenSolver<EigenMatrix> eigenSolver(matrix);

            if (eigenSolver.info() == Eigen::ComputationInfo::Success)
            {
                // We want the eigenvalues in descending order, Eigen produces them in increasing order.
                eigenvalues = eigenSolver.eigenvalues().reverse().head(static_cast<long>(dwMaxRange));
                eigenvectors = eigenSolver.eigenvectors().rowwise().reverse().leftCols(static_cast<long>(dwMaxRange));
                return true;
            }
            else
            {
                DPF(0, "Eigen::SelfAdjointEigenSolver failed with info() == %d", eigenSolver.info());
            }

            return false;
        }
    };

#else // !UVATLAS_USE_EIGEN

    // This file implement the algorithm in "Numerical Recipes in Fortan 77,
    // The Art of Scientific Computing Second Edition", Section 11.1 ~ 11.3
    // http://www.library.cornell.edu/nr/bookfpdf/f11-1.pdf
    // http://www.library.cornell.edu/nr/bookfpdf/f11-2.pdf
    // http://www.library.cornell.edu/nr/bookfpdf/f11-3.pdf

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdouble-promotion"
#endif

    template<class TYPE>
    class CSymmetricMatrix
    {
    public:
        using value_type = TYPE;

    private:
        static inline value_type VectorDot(
            const value_type v1[],
            const value_type v2[],
            size_t dwDimension)
        {
            value_type result = 0;
            for (size_t ii = 0; ii < dwDimension; ii++)
            {
                result += v1[ii] * v2[ii];
            }

            return result;
        }

        static inline void VectorScale(
            value_type v[],
            value_type scale,
            size_t dwDimension)
        {
            assert(std::isfinite(double(scale)));
            for (size_t ii = 0; ii < dwDimension; ii++)
            {
                v[ii] *= scale;
            }
        }

        static inline void VectorZero(
            value_type v[],
            size_t dwDimension)
        {
            memset(v, 0, dwDimension * sizeof(value_type));
        }

        static inline void VectorAssign(
            value_type dest[],
            const value_type src[],
            size_t dwDimension)
        {
            memcpy(dest, src, dwDimension * sizeof(value_type));
        }

    public:
        _Success_(return)
            static bool
            GetEigen(
                size_t dwDimension,
                _In_reads_(dwDimension * dwDimension) const value_type* pMatrix,
                _Out_writes_(dwMaxRange) value_type* pEigenValue,
                _Out_writes_(dwDimension * dwMaxRange) value_type* pEigenVector,
                size_t dwMaxRange,
                value_type epsilon = 1.0e-6f)
        {
            // 1. check argument
            if (!pMatrix || !pEigenValue || !pEigenVector)
                return false;

            if (dwDimension < dwMaxRange
                || dwMaxRange == 0
                || dwDimension == 0)
            {
                return false;
            }

            // 2. allocate memory resouce
            std::unique_ptr<value_type[]> tmp(new (std::nothrow) value_type[(dwDimension * dwDimension) + (4 * dwDimension)]);
            if (!tmp)
                return false;

            value_type* pInitialMatrix = tmp.get();                                 // dwDimension * dwDimension
            value_type* pSubDiagVec = pInitialMatrix + (dwDimension * dwDimension); // dwDimension
            value_type* pU = pSubDiagVec + dwDimension;                             // dwDimension
            value_type* pP = pU + dwDimension;                                      // dwDimension
            value_type* pValues = pP + dwDimension;                                 // dwDimension

            std::unique_ptr<value_type * []> rowHeader(new (std::nothrow) value_type * [dwDimension]);
            if (!rowHeader)
                return false;

            value_type** pRowHeader = rowHeader.get();

            VectorZero(pSubDiagVec, dwDimension);
            VectorAssign(pInitialMatrix, pMatrix, dwDimension * dwDimension);

            for (size_t i = 0; i < dwDimension; i++)
            {
                pRowHeader[i] = pInitialMatrix + i * dwDimension;
            }

            // 3. Using Householder method to reduction to tridiagonal matrix

            // 3.1 Prepare u vector of first iteration.
            memcpy(pU, pRowHeader[dwDimension - 1], dwDimension * sizeof(value_type));

            for (size_t i = dwDimension - 1; i > 0; i--)
            {
                value_type total = 0;
                value_type h = 0;
                for (size_t j = 0; j < i; j++)
                {
                    total += static_cast<value_type>(fabs(pU[j]));
                }

                if (total < epsilon)
                {
                    // prepare u of next iteration, skip this step
                    pU[i] = 0;
                    for (size_t j = 0; j < i; j++)
                    {
                        pU[j] = pRowHeader[i - 1][j];
                        pRowHeader[i][j] = 0;
                        pRowHeader[j][i] = 0;
                    }
                }
                else
                {
                    value_type scale = 1.0f / total;

                    VectorScale(pU, scale, i);
                    h = VectorDot(pU, pU, i);

                    //value_type shift = pValues[i - 1];
                    auto g = (pU[i - 1] < 0) ? value_type(-IsochartSqrt(h)) : value_type(IsochartSqrt(h));

                    pSubDiagVec[i] = -(total * g); // i element of sub-diagonal vector
                    h += pU[i - 1] * g; // h = |u|*|u|/2
                    pU[i - 1] += g; // u(i-1) = u(i-1) + |g|

                    VectorZero(pP, i);
                    // compute p = A * u / H, Used property of symmetric Matrix
                    for (size_t j = 0; j < i; j++)
                    {
                        pRowHeader[j][i] = pU[j];
                        pP[j] += pRowHeader[j][j] * pU[j];
                        for (size_t k = 0; k < j; k++)
                        {
                            pP[j] += pRowHeader[j][k] * pU[k];
                            pP[k] += pRowHeader[j][k] * pU[j];
                        }
                    }

                    VectorScale(pP, 1.0f / h, i);
                    // compute K = u'* p / (2*H)
                    value_type up = VectorDot(pU, pP, i);
                    value_type K = up / (h + h);

                    // compute q = p - K * u, store q into p for p will not be used
                    // any more.
                    for (size_t j = 0; j < i; j++)
                    {
                        pP[j] -= K * pU[j];
                    }

                    // compute A` = A - q * u' - u * q', only need to compute lower
                    // triangle

                    for (size_t j = 0; j < i; j++)
                    {
                        for (size_t k = j; k < i; k++)
                        {
                            pRowHeader[k][j] -= (pP[k] * pU[j] + pP[j] * pU[k]);
                            if (fabs(pRowHeader[k][j]) < epsilon)
                            {
                                pRowHeader[k][j] = 0;
                            }
                        }
                    }

                    // prepare u vector for next iteration
                    for (size_t j = 0; j < i; j++)
                    {
                        pU[j] = pRowHeader[i - 1][j];
                        pRowHeader[i][j] = 0.0;
                    }
                }
                // After i iteration, pU[i] will never used in u vector, so store H of
                // this iteration in it.
                pU[i] = h;
            }

            // Q = P(0) * P(1) * p(2) * * * p(n-1)
            // Q(n-1) = P(n-1)
            // Q(n-2) = P(n-2) * Q(n-1)
            //     ......
            // Q(0) = Q = P(0) * Q(1)

            // Here used :
            //P*Q = ( 1 - u* u'/H)Q
            //= Q - u * u' * Q / H   ( 2n*n multiplication )
            //= Q - (u/H) * (u' * Q); ( n*n +n multiplication )
            for (size_t i = 0; i < dwDimension - 1; i++)
            {
                pValues[i] = pRowHeader[i][i];
                pRowHeader[i][i] = 1.0;

                size_t currentDim = i + 1;

                if (fabs(pU[currentDim]) > epsilon)
                {
                    //Q - (u/H) * (u' * Q); ( n*n +n multiplication )
                    for (size_t j = 0; j < currentDim; j++)
                    {
                        value_type delta = 0.0;

                        for (size_t k = 0; k < currentDim; k++)
                        {
                            delta += pRowHeader[k][j] * pRowHeader[k][currentDim];
                        }

                        for (size_t k = 0; k <= i; k++)
                        {
                            pRowHeader[k][j] -=
                                delta *
                                pRowHeader[k][currentDim]
                                / pU[currentDim];
                        }
                    }
                }
                for (size_t k = 0; k <= i; k++)
                {
                    pRowHeader[k][currentDim] = 0;
                }
            }
            pValues[dwDimension - 1] = pRowHeader[dwDimension - 1][dwDimension - 1];

            pRowHeader[dwDimension - 1][dwDimension - 1] = 1;
            VectorZero(pRowHeader[dwDimension - 1], dwDimension - 1);

            // 2. Symmetric tridiagonal QL algorithm.
            // 2.1 For Convience, renumber the element of subdiagal vector
            memmove(
                pSubDiagVec, pSubDiagVec + 1,
                (dwDimension - 1) * sizeof(value_type));

            pSubDiagVec[dwDimension - 1] = 0;

            value_type shift = 0;
            value_type maxv = 0;

            // 2.2 QL iteration Algorithm.
            for (size_t j = 0; j < dwDimension; j++)
            {
                // 2.2.1 Find a small subdiagonal element to split the matrix
                auto temp = value_type(fabs(pValues[j]) + fabs(pSubDiagVec[j]));
                if (maxv < temp)
                {
                    maxv = temp;
                }

                // Iteration to zero the  subdiagal item e[j]
                for (;;)
                {
                    size_t n;
                    for (n = j; n < dwDimension; n++)
                    {
                        if (fabs(pSubDiagVec[n]) <= epsilon * maxv)
                        {
                            break;
                        }
                    }

                    // e[j] already equals to 0, get eigenvalue d[j] by adding back
                    // shift value.
                    if (n == j)
                    {
                        pValues[j] = pValues[j] + shift;
                        pSubDiagVec[j] = 0.0;
                        break;
                    }
                    // A plane rotation as Original OL, followed by n-j-2 Given rotations to
                    // restore tridiagonal form
                    else
                    {
                        // Estimate the shift value by comptuing the eigenvalues of leading
                        // 2-dimension matrix. Usually, we get 2 eigenvalues, use the one
                        // close to pValues[j]
                        value_type a = 1;
                        value_type b = -(pValues[j] + pValues[j + 1]);
                        value_type c =
                            pValues[j] * pValues[j + 1]
                            - pSubDiagVec[j] * pSubDiagVec[j];

                        auto bc = value_type(IsochartSqrt(b * b - 4 * a * c));
                        value_type ks = (-b + bc) / 2;
                        value_type ks1 = (-b - bc) / 2;

                        if (fabs(pValues[j] - ks) >
                            fabs(pValues[j] - ks1))
                        {
                            ks = ks1;
                        }

                        // Shift original matrix.
                        for (size_t k = j; k < dwDimension; k++)
                        {
                            pValues[k] -= ks;
                        }

                        // Record the totoal shift value.
                        shift = shift + ks;

                        value_type extra;
                        value_type lastqq;
                        value_type lastpp;
                        value_type lastpq;
                        value_type lastC;
                        value_type lastS;

                        // "Jacobi Rotation" at P(n-1, n)
                        // C = d(n) / (d(n)^2 + e(n-1)^2)
                        // S = e(n-1) / (d(n)^2 + e(n-1)^2)
                        auto tt = value_type(IsochartSqrt(
                            pValues[n] * pValues[n] +
                            pSubDiagVec[n - 1] * pSubDiagVec[n - 1]));

                        lastC = pValues[n] / tt;
                        lastS = pSubDiagVec[n - 1] / tt;

                        lastqq =
                            lastS * lastS * pValues[n - 1]
                            + lastC * lastC * pValues[n]
                            + 2 * lastS * lastC * pSubDiagVec[n - 1];
                        lastpp =
                            lastS * lastS * pValues[n]
                            + lastC * lastC * pValues[n - 1]
                            - 2 * lastS * lastC * pSubDiagVec[n - 1];
                        lastpq =
                            (lastC * lastC - lastS * lastS) * pSubDiagVec[n - 1]
                            + lastS * lastC * (pValues[n - 1] - pValues[n]);

                        // Because d[n-1], e[n-1] will continue to be changed in next
                        // step, only change d[n] here
                        pValues[n] = value_type(lastqq);

                        // Multiply current rotoation matrix to the finial orthogonal matrix,
                        //which stores the eigenvectors
                        for (size_t l = 0; l < dwDimension; l++)
                        {
                            value_type tempItem = pRowHeader[l][n];
                            pRowHeader[l][n] = value_type(
                                lastS * pRowHeader[l][n - 1]
                                + lastC * tempItem);
                            pRowHeader[l][n - 1] = value_type(
                                lastC * pRowHeader[l][n - 1]
                                - lastS * tempItem);
                        }
                        // If need restore tridiagonal form
                        if (n > j + 1)
                        {
                            // Each step, generate a Given rotation matrix to decrease
                            // the "extra" item.
                            // Each step, e[next+1] and d[next+1] can be decided.
                            // Each step, compute a new "extra" value.
                            extra = lastS * pSubDiagVec[n - 2];
                            assert(n > 1);
                            size_t  next;
                            for (size_t k = n - 1; k > j; k--)
                            {
                                next = k - 1;
                                pSubDiagVec[next] = value_type(lastC * pSubDiagVec[next]);
                                tt = value_type(IsochartSqrt(lastpq * lastpq + extra * extra));
                                lastC = lastpq / tt;
                                lastS = extra / tt;

                                pSubDiagVec[next + 1] = value_type(lastC * lastpq + lastS * extra);

                                pValues[next + 1] = value_type(
                                    lastS * lastS * pValues[next]
                                    + lastC * lastC * lastpp
                                    + 2 * lastS * lastC * pSubDiagVec[next]);

                                lastpq =
                                    (lastC * lastC - lastS * lastS) * pSubDiagVec[next]
                                    + lastS * lastC * (pValues[next] - lastpp);

                                lastpp =
                                    lastS * lastS * lastpp
                                    + lastC * lastC * pValues[next]
                                    - 2 * lastS * lastC * pSubDiagVec[next];

                                if (next > 0)
                                    extra = lastS * pSubDiagVec[next - 1];

                                for (size_t l = 0; l < dwDimension; l++)
                                {
                                    value_type tempItem = pRowHeader[l][next + 1];
                                    pRowHeader[l][next + 1] = value_type(
                                        lastS * pRowHeader[l][next]
                                        + lastC * tempItem);
                                    pRowHeader[l][next] = value_type(
                                        lastC * pRowHeader[l][next]
                                        - lastS * tempItem);
                                }
                            }
                        }

                        // Last step.
                        pValues[j] = value_type(lastpp);
                        pSubDiagVec[j] = value_type(lastpq);
                        if (n < dwDimension)
                        {
                            pSubDiagVec[n] = 0.0;
                        }
                    }
                }
            }

            // Sort eigenvalues and corresponding vectors.
            for (size_t i = 0; i < dwDimension - 1; i++)
            {
                for (size_t j = i + 1; j < dwDimension; j++)
                {
                    if (pValues[j] > pValues[i])
                    {
                        std::swap(pValues[i], pValues[j]);

                        for (size_t k = 0; k < dwDimension; k++)
                        {
                            std::swap(pRowHeader[k][i], pRowHeader[k][j]);
                        }
                    }
                }
            }

            // Export the selected eigen values and vectors
            for (size_t i = 0; i < dwMaxRange; i++)
            {
                pEigenValue[i] = pValues[i];

                for (size_t j = 0; j < dwDimension; j++)
                {
                    pEigenVector[i * dwDimension + j] = pRowHeader[j][i];
                }
            }

            return true;
        }
    };

#ifdef __clang__
#pragma clang diagnostic pop
#endif
#endif
}
