/*
 *  atexit - allow programmer to define multiple exit functions to be executed
 *  upon normal program termination.
 *
 *   Translated from atexit.py by Collin Winter.
 +   Copyright 2007 Python Software Foundation.
 */

#include "Python.h"
#include "pycore_atexit.h"        // export _Py_AtExit()
#include "pycore_initconfig.h"    // _PyStatus_NO_MEMORY
#include "pycore_interp.h"        // PyInterpreterState.atexit
#include "pycore_pystate.h"       // _PyInterpreterState_GET

#ifdef Py_GIL_DISABLED
/* Note: anything declared as static in this file assumes the lock is held
 * (except for the Python-level functions) */
#  define _PyAtExit_LOCK(state) PyMutex_Lock(&state->lock);
#  define _PyAtExit_UNLOCK(state) PyMutex_Unlock(&state->lock);
#else
#  define _PyAtExit_LOCK(state)
#  define _PyAtExit_UNLOCK(state)
#endif

/* ===================================================================== */
/* Callback machinery. */

static inline struct atexit_state*
get_atexit_state(void)
{
    PyInterpreterState *interp = _PyInterpreterState_GET();
    return &interp->atexit;
}


int
PyUnstable_AtExit(PyInterpreterState *interp,
                  atexit_datacallbackfunc func, void *data)
{
    assert(interp == _PyInterpreterState_GET());
    atexit_callback *callback = PyMem_Malloc(sizeof(atexit_callback));
    if (callback == NULL) {
        PyErr_NoMemory();
        return -1;
    }
    callback->func = func;
    callback->data = data;
    callback->next = NULL;

    struct atexit_state *state = &interp->atexit;
    _PyAtExit_LOCK(state);
    if (state->ll_callbacks == NULL) {
        state->ll_callbacks = callback;
        state->last_ll_callback = callback;
    }
    else {
        state->last_ll_callback->next = callback;
    }
    _PyAtExit_UNLOCK(state);
    return 0;
}


static void
atexit_delete_cb(struct atexit_state *state, int i)
{
    atexit_py_callback *cb = state->callbacks[i];
    state->callbacks[i] = NULL;

    // We don't need to hold the lock; the entry isn't in the array anymore
    _PyAtExit_UNLOCK(state);

    Py_DECREF(cb->func);
    Py_DECREF(cb->args);
    Py_XDECREF(cb->kwargs);
    PyMem_Free(cb);
}


/* Clear all callbacks without calling them */
static void
atexit_cleanup(struct atexit_state *state)
{
    atexit_py_callback *cb;
    for (int i = 0; i < state->ncallbacks; i++) {
        cb = state->callbacks[i];
        if (cb == NULL)
            continue;

        atexit_delete_cb(state, i);
    }
    state->ncallbacks = 0;
}


PyStatus
_PyAtExit_Init(PyInterpreterState *interp)
{
    struct atexit_state *state = &interp->atexit;
    // _PyAtExit_Init() must only be called once
    assert(state->callbacks == NULL);

    state->callback_len = 32;
    state->ncallbacks = 0;
    state->callbacks = PyMem_New(atexit_py_callback*, state->callback_len);
    if (state->callbacks == NULL) {
        return _PyStatus_NO_MEMORY();
    }
    return _PyStatus_OK();
}


void
_PyAtExit_Fini(PyInterpreterState *interp)
{
    struct atexit_state *state = &interp->atexit;
    // XXX Can this be unlocked?
    _PyAtExit_LOCK(state);
    atexit_cleanup(state);
    PyMem_Free(state->callbacks);
    state->callbacks = NULL;

    atexit_callback *next = state->ll_callbacks;
    state->ll_callbacks = NULL;
    while (next != NULL) {
        atexit_callback *callback = next;
        next = callback->next;
        atexit_datacallbackfunc exitfunc = callback->func;
        void *data = callback->data;
        // It was allocated in _PyAtExit_AddCallback().
        PyMem_Free(callback);
        exitfunc(data);
    }
    _PyAtExit_UNLOCK(state);
}


static void
atexit_callfuncs(struct atexit_state *state)
{
    assert(!PyErr_Occurred());

    if (state->ncallbacks == 0) {
        return;
    }

    for (int i = state->ncallbacks - 1; i >= 0; i--) {
        atexit_py_callback *cb = state->callbacks[i];
        if (cb == NULL) {
            continue;
        }

        // bpo-46025: Increment the refcount of cb->func as the call itself may unregister it
        PyObject *func = Py_NewRef(cb->func);
        // No need to hold a strong reference to the arguments though
        PyObject *args = cb->args;
        PyObject *kwargs = cb->kwargs;

        // Unlock for re-entrancy problems
        _PyAtExit_UNLOCK(state);
        PyObject *res = PyObject_Call(func, args, kwargs);
        if (res == NULL) {
            PyErr_FormatUnraisable(
                "Exception ignored in atexit callback %R", the_func);
        }
        else {
            Py_DECREF(res);
        }
        Py_DECREF(func);
        _PyAtExit_LOCK(state);
    }

    atexit_cleanup(state);

    assert(!PyErr_Occurred());
}


void
_PyAtExit_Call(PyInterpreterState *interp)
{
    struct atexit_state *state = &interp->atexit;
    _PyAtExit_LOCK(state);
    atexit_callfuncs(state);
    _PyAtExit_UNLOCK(state);
}


/* ===================================================================== */
/* Module methods. */


PyDoc_STRVAR(atexit_register__doc__,
"register($module, func, /, *args, **kwargs)\n\
--\n\
\n\
Register a function to be executed upon normal program termination\n\
\n\
    func - function to be called at exit\n\
    args - optional arguments to pass to func\n\
    kwargs - optional keyword arguments to pass to func\n\
\n\
    func is returned to facilitate usage as a decorator.");

static PyObject *
atexit_register(PyObject *module, PyObject *args, PyObject *kwargs)
{
    if (PyTuple_GET_SIZE(args) == 0) {
        PyErr_SetString(PyExc_TypeError,
                "register() takes at least 1 argument (0 given)");
        return NULL;
    }

    PyObject *func = PyTuple_GET_ITEM(args, 0);
    if (!PyCallable_Check(func)) {
        PyErr_SetString(PyExc_TypeError,
                "the first argument must be callable");
        return NULL;
    }

    struct atexit_state *state = get_atexit_state();
    /* In theory, we could hold the lock for a shorter amount of time
     * using some fancy compare-exchanges with the length. However, I'm lazy.
     */
    _PyAtExit_LOCK(state);
    if (state->ncallbacks >= state->callback_len) {
        atexit_py_callback **r;
        state->callback_len += 16;
        size_t size = sizeof(atexit_py_callback*) * (size_t)state->callback_len;
        r = (atexit_py_callback**)PyMem_Realloc(state->callbacks, size);
        if (r == NULL) {
            _PyAtExit_UNLOCK(state);
            return PyErr_NoMemory();
        }
        state->callbacks = r;
    }

    atexit_py_callback *callback = PyMem_Malloc(sizeof(atexit_py_callback));
    if (callback == NULL) {
        _PyAtExit_UNLOCK(state);
        return PyErr_NoMemory();
    }

    callback->args = PyTuple_GetSlice(args, 1, PyTuple_GET_SIZE(args));
    if (callback->args == NULL) {
        PyMem_Free(callback);
        _PyAtExit_UNLOCK(state);
        return NULL;
    }
    callback->func = Py_NewRef(func);
    callback->kwargs = Py_XNewRef(kwargs);

    state->callbacks[state->ncallbacks++] = callback;
    _PyAtExit_UNLOCK(state);

    return Py_NewRef(func);
}

PyDoc_STRVAR(atexit_run_exitfuncs__doc__,
"_run_exitfuncs($module, /)\n\
--\n\
\n\
Run all registered exit functions.\n\
\n\
If a callback raises an exception, it is logged with sys.unraisablehook.");

static PyObject *
atexit_run_exitfuncs(PyObject *module, PyObject *unused)
{
    struct atexit_state *state = get_atexit_state();
    _PyAtExit_LOCK(state);
    atexit_callfuncs(state);
    _PyAtExit_UNLOCK(state);
    Py_RETURN_NONE;
}

PyDoc_STRVAR(atexit_clear__doc__,
"_clear($module, /)\n\
--\n\
\n\
Clear the list of previously registered exit functions.");

static PyObject *
atexit_clear(PyObject *module, PyObject *unused)
{
    struct atexit_state *state = get_atexit_state();
    _PyAtExit_LOCK(state);
    atexit_cleanup(state);
    _PyAtExit_UNLOCK(state);
    Py_RETURN_NONE;
}

PyDoc_STRVAR(atexit_ncallbacks__doc__,
"_ncallbacks($module, /)\n\
--\n\
\n\
Return the number of registered exit functions.");

static PyObject *
atexit_ncallbacks(PyObject *module, PyObject *unused)
{
    struct atexit_state *state = get_atexit_state();
    _PyAtExit_LOCK(state);
    PyObject *res = PyLong_FromSsize_t(state->ncallbacks);
    _PyAtExit_UNLOCK(state);
    return res;
}

PyDoc_STRVAR(atexit_unregister__doc__,
"unregister($module, func, /)\n\
--\n\
\n\
Unregister an exit function which was previously registered using\n\
atexit.register\n\
\n\
    func - function to be unregistered");

static PyObject *
atexit_unregister(PyObject *module, PyObject *func)
{
    struct atexit_state *state = get_atexit_state();
    _PyAtExit_LOCK(state);
    for (int i = 0; i < state->ncallbacks; i++)
    {
        atexit_py_callback *cb = state->callbacks[i];
        if (cb == NULL) {
            continue;
        }
        PyObject *to_compare = cb->func;

        // Unlock for custom __eq__ causing re-entrancy
        _PyAtExit_UNLOCK(state);
        int eq = PyObject_RichCompareBool(to_compare, func, Py_EQ);
        if (eq < 0) {
            return NULL;
        }
        _PyAtExit_LOCK(state);
        if (state->callbacks[i] == NULL)
        {
            // Edge case: another thread might have
            // unregistered the function while we released the lock.
            continue;
        }
        if (eq) {
            atexit_delete_cb(state, i);
        }
    }
    _PyAtExit_UNLOCK(state);
    Py_RETURN_NONE;
}


static PyMethodDef atexit_methods[] = {
    {"register", _PyCFunction_CAST(atexit_register), METH_VARARGS|METH_KEYWORDS,
        atexit_register__doc__},
    {"_clear", (PyCFunction) atexit_clear, METH_NOARGS,
        atexit_clear__doc__},
    {"unregister", (PyCFunction) atexit_unregister, METH_O,
        atexit_unregister__doc__},
    {"_run_exitfuncs", (PyCFunction) atexit_run_exitfuncs, METH_NOARGS,
        atexit_run_exitfuncs__doc__},
    {"_ncallbacks", (PyCFunction) atexit_ncallbacks, METH_NOARGS,
        atexit_ncallbacks__doc__},
    {NULL, NULL}        /* sentinel */
};


/* ===================================================================== */
/* Initialization function. */

PyDoc_STRVAR(atexit__doc__,
"allow programmer to define multiple exit functions to be executed\n\
upon normal program termination.\n\
\n\
Two public functions, register and unregister, are defined.\n\
");

static PyModuleDef_Slot atexitmodule_slots[] = {
    {Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED},
    {Py_mod_gil, Py_MOD_GIL_NOT_USED},
    {0, NULL}
};

static struct PyModuleDef atexitmodule = {
    PyModuleDef_HEAD_INIT,
    .m_name = "atexit",
    .m_doc = atexit__doc__,
    .m_size = 0,
    .m_methods = atexit_methods,
    .m_slots = atexitmodule_slots,
};

PyMODINIT_FUNC
PyInit_atexit(void)
{
    return PyModuleDef_Init(&atexitmodule);
}
