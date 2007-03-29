/* Copyright (C) 2003-2005 Peter J. Verveer
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ND_IMAGE_H
#define ND_IMAGE_H

#include "Python.h"
#include <numpy/noprefix.h>

#define NI_MAXDIM NPY_MAXDIMS

typedef npy_intp maybelong;
#define MAXDIM NPY_MAXDIMS

typedef enum
{
     tAny=-1,
     tBool=PyArray_BOOL,
     tInt8=PyArray_INT8,
     tUInt8=PyArray_UINT8,
     tInt16=PyArray_INT16,
     tUInt16=PyArray_UINT16,
     tInt32=PyArray_INT32,
     tUInt32=PyArray_UINT32,
     tInt64=PyArray_INT64,
     tUInt64=PyArray_UINT64,
     tFloat32=PyArray_FLOAT32,
     tFloat64=PyArray_FLOAT64,
     tComplex32=PyArray_COMPLEX64,
     tComplex64=PyArray_COMPLEX128,
     tObject=PyArray_OBJECT,        /* placeholder... does nothing */
     tMaxType=PyArray_NTYPES,
     tDefault = tFloat64,
#if NPY_BITSOF_LONG == 64
     tLong = tInt64,
#else
     tLong = tInt32,
#endif
} NumarrayType;

/* satisfies ensures that 'a' meets a set of requirements and matches
the specified type.
*/
static int
satisfies(PyArrayObject *a, int requirements, NumarrayType t)
{
    int type_ok = (a->descr->type_num == t) || (t == tAny);

    if (PyArray_ISCARRAY(a))
        return type_ok;
    if (PyArray_ISBYTESWAPPED(a) && (requirements & NPY_NOTSWAPPED))
        return 0;
    if (!PyArray_ISALIGNED(a) && (requirements & NPY_ALIGNED))
        return 0;
    if (!PyArray_ISCONTIGUOUS(a) && (requirements & NPY_CONTIGUOUS))
        return 0;
    if (!PyArray_ISWRITEABLE(a) && (requirements & NPY_WRITEABLE))
        return 0;
    if (requirements & NPY_ENSURECOPY)
        return 0;
    return type_ok;
}

static PyArrayObject*
NA_InputArray(PyObject *a, NumarrayType t, int requires)
{
    PyArray_Descr *descr;
    if (t == tAny) descr = NULL;
    else descr = PyArray_DescrFromType(t);
    return (PyArrayObject *)                                            \
        PyArray_CheckFromAny(a, descr, 0, 0, requires, NULL);
}

static PyArrayObject *
NA_OutputArray(PyObject *a, NumarrayType t, int requires)
{
    PyArray_Descr *dtype;
    PyArrayObject *ret;

    if (!PyArray_Check(a) || !PyArray_ISWRITEABLE(a)) {
        PyErr_Format(PyExc_TypeError,
                     "NA_OutputArray: only writeable arrays work for output.");
        return NULL;
    }

    if (satisfies((PyArrayObject *)a, requires, t)) {
        Py_INCREF(a);
        return (PyArrayObject *)a;
    }
    if (t == tAny) {
        dtype = PyArray_DESCR(a);
        Py_INCREF(dtype);
    }
    else {
        dtype = PyArray_DescrFromType(t);
    }
    ret = (PyArrayObject *)PyArray_Empty(PyArray_NDIM(a), PyArray_DIMS(a),
                                         dtype, 0);
    ret->flags |= NPY_UPDATEIFCOPY;
    ret->base = a;
    PyArray_FLAGS(a) &= ~NPY_WRITEABLE;
    Py_INCREF(a);
    return ret;
}

/* NA_IoArray is a combination of NA_InputArray and NA_OutputArray.

Unlike NA_OutputArray, if a temporary is required it is initialized to a copy
of the input array.

Unlike NA_InputArray, deallocating any resulting temporary array results in a
copy from the temporary back to the original.
*/
static PyArrayObject *
NA_IoArray(PyObject *a, NumarrayType t, int requires)
{
    PyArrayObject *shadow = NA_InputArray(a, t, requires | NPY_UPDATEIFCOPY );

    if (!shadow) return NULL;

    /* Guard against non-writable, but otherwise satisfying requires.
       In this case,  shadow == a.
    */
    if (!PyArray_ISWRITEABLE(shadow)) {
        PyErr_Format(PyExc_TypeError,
                     "NA_IoArray: I/O array must be writable array");
        PyArray_XDECREF_ERR(shadow);
        return NULL;
    }

    return shadow;
}

static unsigned long
NA_elements(PyArrayObject  *a)
{
    int i;
    unsigned long n = 1;
    for(i = 0; i<a->nd; i++)
        n *= a->dimensions[i];
    return n;
}

#define NUM_LITTLE_ENDIAN 0
#define NUM_BIG_ENDIAN 1

static int
NA_ByteOrder(void)
{
    unsigned long byteorder_test;
    byteorder_test = 1;
    if (*((char *) &byteorder_test))
        return NUM_LITTLE_ENDIAN;
    else
        return NUM_BIG_ENDIAN;
}

/* ignores bytestride */
static PyArrayObject *
NA_NewAllFromBuffer(int ndim, maybelong *shape, NumarrayType type,
                    PyObject *bufferObject, maybelong byteoffset, maybelong bytestride,
                    int byteorder, int aligned, int writeable)
{
    PyArrayObject *self = NULL;
    PyArray_Descr *dtype;

    if (type == tAny)
        type = tDefault;

    dtype = PyArray_DescrFromType(type);
    if (dtype == NULL) return NULL;

    if (byteorder != NA_ByteOrder()) {
        PyArray_Descr *temp;
        temp = PyArray_DescrNewByteorder(dtype, PyArray_SWAP);
        Py_DECREF(dtype);
        if (temp == NULL) return NULL;
        dtype = temp;
    }

    if (bufferObject == Py_None || bufferObject == NULL) {
        self = (PyArrayObject *)                                        \
            PyArray_NewFromDescr(&PyArray_Type, dtype,
                                 ndim, shape, NULL, NULL,
                                 0, NULL);
    }
    else {
        npy_intp size = 1;
        int i;
        PyArrayObject *newself;
        PyArray_Dims newdims;
        for(i=0; i<ndim; i++) {
            size *= shape[i];
        }
        self = (PyArrayObject *)                                \
            PyArray_FromBuffer(bufferObject, dtype,
                               size, byteoffset);
        if (self == NULL) return self;
        newdims.len = ndim;
        newdims.ptr = shape;
        newself = (PyArrayObject *)                                     \
            PyArray_Newshape(self, &newdims, PyArray_CORDER);
        Py_DECREF(self);
        self = newself;
    }

    return self;
}

#define NA_NBYTES(a) (a->descr->elsize * NA_elements(a))

static PyArrayObject *
NA_NewAll(int ndim, maybelong *shape, NumarrayType type,
          void *buffer, maybelong byteoffset, maybelong bytestride,
          int byteorder, int aligned, int writeable)
{
    PyArrayObject *result = NA_NewAllFromBuffer(
                                                ndim, shape, type, Py_None,
                                                byteoffset, bytestride,
                                                byteorder, aligned, writeable);
    if (result) {
        if (!PyArray_Check((PyObject *) result)) {
            PyErr_Format( PyExc_TypeError,
                          "NA_NewAll: non-NumArray result");
            result = NULL;
        } else {
            if (buffer) {
                memcpy(result->data, buffer, NA_NBYTES(result));
            } else {
                memset(result->data, 0, NA_NBYTES(result));
            }
        }
    }
    return  result;
}

/* Create a new numarray which is initially a C_array, or which
references a C_array: aligned, !byteswapped, contiguous, ...
Call with buffer==NULL to allocate storage.
*/
static PyArrayObject *
NA_NewArray(void *buffer, NumarrayType type, int ndim, maybelong *shape)
{
    return (PyArrayObject *) NA_NewAll(ndim, shape, type, buffer, 0, 0,
                                       NA_ByteOrder(), 1, 1);
}


#define  NA_InputArray (*(PyArrayObject* (*) (PyObject*,NumarrayType,int) ) (void *) NA_InputArray)
#define  NA_OutputArray (*(PyArrayObject* (*) (PyObject*,NumarrayType,int) ) (void *) NA_OutputArray)
#define  NA_IoArray (*(PyArrayObject* (*) (PyObject*,NumarrayType,int) ) (void *) NA_IoArray)
#define  NA_elements (*(unsigned long (*) (PyArrayObject*) ) (void *) NA_elements)
#define  NA_NewArray (*(PyArrayObject* (*) (void* buffer, NumarrayType type, int ndim, ...) ) (void *) NA_NewArray )

#endif
