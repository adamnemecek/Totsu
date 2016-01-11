/*
* This software contains source code provided by NVIDIA Corporation.
*
*/
/*
* Copyright 2015 NVIDIA Corporation.  All rights reserved.
*
* Please refer to the NVIDIA end user license agreement (EULA) associated
* with this source code for terms and conditions that govern your use of
* this software. Any use, reproduction, disclosure, or distribution of
* this software and related documentation outside the terms of the EULA
* is strictly prohibited.
*
*/

#ifndef _NUM_CALC_GPU_H_
#define _NUM_CALC_GPU_H_

#include <cuda_runtime.h>
#include <cusolverDn.h>
#include <float.h>

typedef double NC_Scalar;
#define NC_SCALAR_MIN DBL_MIN

typedef unsigned int NC_uint;

/********************************************************************/

class NCMem_GPU
{
public:
	explicit NCMem_GPU() :
		m_nMemSize(0), m_pMem(NULL), m_pMemHost(NULL)
	{}
	virtual ~NCMem_GPU() { free(); }
private:
	// uncopyable
	NCMem_GPU(const NCMem_GPU&);
	NCMem_GPU& operator=(const NCMem_GPU&);

public:
	int copyToHost(void)
	{
		if (m_pMem) {
			_hostPtr();
			if (!memcpyToHost()) return 0;
		}
		return 1; // failed
	}
	int copyToDevice(void)
	{
		if (m_pMem && m_pMemHost) {
			if (!memcpyToDevice()) return 0;
		}
		return 1; // failed
	}

	int setZero(void)
	{
		if (m_pMem) {
			if (!cudaMemset(m_pMem, 0, m_nMemSize)) return 0;
		}
		return 1; // failed
	}

protected:
	void free()
	{
		if (m_pMem) cudaFree(m_pMem);
		if (m_pMemHost) delete m_pMemHost;

		m_nMemSize = 0;
		m_pMem = NULL;
		m_pMemHost = NULL;
	}

	void* _ptr(void) { return m_pMem; }
	char* _hostPtr(void)
	{
		if (!m_pMemHost) {
			m_pMemHost = new char[m_nMemSize];
		}

		return m_pMemHost;
	}

	virtual int memcpyToHost(void) = 0;
	virtual int memcpyToDevice(void) = 0;

protected:
	NC_uint  m_nMemSize;
	void    *m_pMem;
	char    *m_pMemHost;
};

/********************************************************************/

class NCBuf_GPU : public NCMem_GPU
{
public:
	explicit NCBuf_GPU() {}
	virtual ~NCBuf_GPU() {}
private:
	// uncopyable
	NCBuf_GPU(const NCBuf_GPU&);
	NCBuf_GPU& operator=(const NCBuf_GPU&);

public:
	void realloc(NC_uint nSize)
	{
		if (m_nMemSize < nSize)
		{
			free();

			int r = cudaMalloc(&m_pMem, nSize);
			m_nMemSize = nSize;

			m_nSize = nSize;

			if (r) { // failed
				free();
				m_nSize = 0;
			}
		}
		else {
			m_nSize = nSize;
		}
	}

	template < class TYPE = void* > TYPE ptr(void)
	{
		return reinterpret_cast<TYPE>(_ptr());
	}

	template < class TYPE = void* > TYPE hostPtr(void)
	{
		return reinterpret_cast<TYPE>(_hostPtr());
	}

protected:
	virtual int memcpyToHost(void)
	{
		return cudaMemcpy(m_pMemHost, m_pMem, m_nSize, cudaMemcpyDeviceToHost);
	}
	virtual int memcpyToDevice(void)
	{
		return cudaMemcpy(m_pMem, m_pMemHost, m_nSize, cudaMemcpyHostToDevice);
	}

protected:
	NC_uint m_nSize;
};

/********************************************************************/

class NCMat_GPU : public NCMem_GPU
{
public:
	explicit NCMat_GPU() :
		m_nRows(0), m_nRowsPitch(0), m_nCols(0), m_nColsPitch(0)
	{}
	virtual ~NCMat_GPU() {}
private:
	// uncopyable
	NCMat_GPU(const NCMat_GPU&);
	NCMat_GPU& operator=(const NCMat_GPU&);

public:
	void resize(NC_uint nRows, NC_uint nCols)
	{
		if ((m_nRowsPitch < nRows) || (m_nColsPitch < nCols))
		{
			free();

			size_t pitchInBytes;
			int r = cudaMallocPitch(&m_pMem, &pitchInBytes, sizeof(NC_Scalar) * nRows, nCols);
			m_nMemSize = NC_uint(pitchInBytes * nCols);

			m_nRowsPitch = NC_uint(pitchInBytes / sizeof(NC_Scalar));
			m_nColsPitch = nCols;
			m_nRows = nRows;
			m_nCols = nCols;

			if (r || (m_nRowsPitch * sizeof(NC_Scalar) != pitchInBytes)) { // failed
				free();

				m_nRowsPitch = 0;
				m_nColsPitch = 0;
				m_nRows = 0;
				m_nCols = 0;
			}
		}
		else {
			m_nRows = nRows;
			m_nCols = nCols;
		}
	}

	NC_Scalar* ptr(void)
	{
		return reinterpret_cast<NC_Scalar*>(_ptr());
	}

	NC_Scalar* hostPtr(void)
	{
		return reinterpret_cast<NC_Scalar*>(_hostPtr());
	}

	NC_uint nRows(void) { return m_nRows; }
	NC_uint nRowsPitch(void) { return m_nRowsPitch; }
	NC_uint nCols(void) { return m_nCols; }

protected:
	virtual int memcpyToHost(void)
	{
		return cudaMemcpy2D(
			m_pMemHost, sizeof(NC_Scalar) * m_nRowsPitch,
			m_pMem, sizeof(NC_Scalar) * m_nRowsPitch,
			sizeof(NC_Scalar) * m_nRows,
			m_nCols,
			cudaMemcpyDeviceToHost);
	}

	virtual int memcpyToDevice(void)
	{
		return cudaMemcpy2D(
			m_pMem, sizeof(NC_Scalar) * m_nRowsPitch,
			m_pMemHost, sizeof(NC_Scalar) * m_nRowsPitch,
			sizeof(NC_Scalar) * m_nRows,
			m_nCols,
			cudaMemcpyHostToDevice);
	}

protected:
	NC_uint m_nRows; // number of rows
	NC_uint m_nRowsPitch;
	NC_uint m_nCols; // number of columns
	NC_uint m_nColsPitch;
};

/********************************************************************/

class NCVec_GPU : public NCMat_GPU
{
public:
	explicit NCVec_GPU() {}
	virtual ~NCVec_GPU() {}
private:
	// uncopyable
	NCVec_GPU(const NCVec_GPU&);
	NCVec_GPU& operator=(const NCVec_GPU&);

public:
	void resize(NC_uint nRows) { NCMat_GPU::resize(nRows, 1); }

private:
	// uncallable
	void resize(NC_uint nRows, NC_uint nCols);
};

/********************************************************************/

class NumCalc_GPU
{
public:
	explicit NumCalc_GPU();
	virtual ~NumCalc_GPU();

	static void resetDevice(void);

	int calcSearchDir(NCMat_GPU &kkt, NCVec_GPU &rtDy); // IO, IO
	int calcMaxScaleBTLS(NCVec_GPU &lmd, NCVec_GPU &Dlmd, NC_Scalar *pSclMax); // I, I, O

private:
	int m_fatalErr;

	cudaDeviceProp     m_cuDevProp;
	cudaStream_t       m_cuStream;
	cusolverDnHandle_t m_cuSolverDn;
	cublasHandle_t     m_cuBlas;

	// calcSearchDir
	NCBuf_GPU m_devInfo;
	NCBuf_GPU m_solverBuf;
	NCBuf_GPU m_solverTau;

	// calcMaxScaleBTLS
	NCBuf_GPU m_sMax;
};

#endif // end of ifndef _NUM_CALC_GPU_H_