/* Cross-interpreter data */

#ifndef Py_INTERNAL_CROSS_INTERPRETER_DATA_H
#define Py_INTERNAL_CROSS_INTERPRETER_DATA_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef Py_BUILD_CORE
#  error "this header requires Py_BUILD_CORE define"
#endif

// _PyCrossInterpreterData is similar to Py_buffer as an effectively
// opaque struct that holds data outside the object machinery.  This
// is necessary to pass safely between interpreters in the same process.
typedef struct _xid _PyCrossInterpreterData;

typedef PyObject *(*xid_newobjectfunc)(_PyCrossInterpreterData *);
typedef void (*xid_freefunc)(void *);

struct _xid {
    // data is the cross-interpreter-safe derivation of a Python object
    // (see _PyObject_GetCrossInterpreterData).  It will be NULL if the
    // new_object func (below) encodes the data.
    void *data;
    // obj is the Python object from which the data was derived.  This
    // is non-NULL only if the data remains bound to the object in some
    // way, such that the object must be "released" (via a decref) when
    // the data is released.  In that case the code that sets the field,
    // likely a registered "crossinterpdatafunc", is responsible for
    // ensuring it owns the reference (i.e. incref).
    PyObject *obj;
    // interp is the ID of the owning interpreter of the original
    // object.  It corresponds to the active interpreter when
    // _PyObject_GetCrossInterpreterData() was called.  This should only
    // be set by the cross-interpreter machinery.
    //
    // We use the ID rather than the PyInterpreterState to avoid issues
    // with deleted interpreters.  Note that IDs are never re-used, so
    // each one will always correspond to a specific interpreter
    // (whether still alive or not).
    int64_t interp;
    // new_object is a function that returns a new object in the current
    // interpreter given the data.  The resulting object (a new
    // reference) will be equivalent to the original object.  This field
    // is required.
    xid_newobjectfunc new_object;
    // free is called when the data is released.  If it is NULL then
    // nothing will be done to free the data.  For some types this is
    // okay (e.g. bytes) and for those types this field should be set
    // to NULL.  However, for most the data was allocated just for
    // cross-interpreter use, so it must be freed when
    // _PyCrossInterpreterData_Release is called or the memory will
    // leak.  In that case, at the very least this field should be set
    // to PyMem_RawFree (the default if not explicitly set to NULL).
    // The call will happen with the original interpreter activated.
    xid_freefunc free;
};

PyAPI_FUNC(int) _PyCrossInterpreterData_InitWithSize(
        _PyCrossInterpreterData *,
        PyInterpreterState *interp, const size_t, PyObject *,
        xid_newobjectfunc);
PyAPI_FUNC(void) _PyCrossInterpreterData_Clear(
        PyInterpreterState *, _PyCrossInterpreterData *);

PyAPI_FUNC(int) _PyObject_GetCrossInterpreterData(PyObject *, _PyCrossInterpreterData *);
PyAPI_FUNC(PyObject*) _PyCrossInterpreterData_NewObject(_PyCrossInterpreterData *);
PyAPI_FUNC(int) _PyCrossInterpreterData_Release(_PyCrossInterpreterData *);

PyAPI_FUNC(int) _PyObject_CheckCrossInterpreterData(PyObject *);

/* cross-interpreter data registry */

typedef int (*crossinterpdatafunc)(PyThreadState *tstate, PyObject *,
                                   _PyCrossInterpreterData *);

// Export for shared _xxinterpchannels extension
PyAPI_FUNC(int) _PyCrossInterpreterData_RegisterClass(PyTypeObject *, crossinterpdatafunc);
PyAPI_FUNC(int) _PyCrossInterpreterData_UnregisterClass(PyTypeObject *);

#ifdef __cplusplus
}
#endif
#endif  // !Py_INTERNAL_CROSS_INTERPRETER_DATA_H
