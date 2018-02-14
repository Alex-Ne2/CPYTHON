/* ABCMeta implementation */

/* TODO: Test this hard in multithreaded context. */
/* TODO: Think about inlining some calls and/or using macros */
/* TODO: Use separate branches with "fast paths" */

#include "Python.h"
#include "structmember.h"

PyDoc_STRVAR(_abc__doc__,
"Module contains faster C implementation of abc.ABCMeta");

_Py_IDENTIFIER(__abstractmethods__);
_Py_IDENTIFIER(__class__);
_Py_IDENTIFIER(__dict__);
_Py_IDENTIFIER(__bases__);
_Py_IDENTIFIER(_abc_impl);
_Py_IDENTIFIER(__subclasscheck__);
_Py_IDENTIFIER(__subclasshook__);

/* A global counter that is incremented each time a class is
   registered as a virtual subclass of anything.  It forces the
   negative cache to be cleared before its next use.
   Note: this counter is private. Use `abc.get_cache_token()` for
   external code. */
static PyObject *abc_invalidation_counter;

/* This object stores internal state for ABCs.
   Note that we can use normal sets for caches,
   since they are never iterated over. */
typedef struct {
    PyObject_HEAD
    PyObject *_abc_registry;
    PyObject *_abc_cache; /* Normal set of weak references. */
    PyObject *_abc_negative_cache; /* Normal set of weak references. */
    PyObject *_abc_negative_cache_version;
} _abc_data;

static void
abc_data_dealloc(_abc_data *self)
{
    Py_XDECREF(self->_abc_registry);
    Py_XDECREF(self->_abc_cache);
    Py_XDECREF(self->_abc_negative_cache);
    Py_DECREF(self->_abc_negative_cache_version);
    Py_TYPE(self)->tp_free(self);
}

static PyObject *
abc_data_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    _abc_data *self = (_abc_data *) type->tp_alloc(type, 0);
    if (self == NULL) {
        return NULL;
    }

    self->_abc_registry = NULL;
    self->_abc_cache = NULL;
    self->_abc_negative_cache = NULL;
    self->_abc_negative_cache_version = abc_invalidation_counter;
    Py_INCREF(abc_invalidation_counter);
    return (PyObject *) self;
}

PyDoc_STRVAR(abc_data_doc,
"Internal state held by ABC machinery.");

static PyTypeObject _abc_data_type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "_abc_data",                        /*tp_name*/
    sizeof(_abc_data),                  /*tp_size*/
    .tp_dealloc = (destructor)abc_data_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_alloc = PyType_GenericAlloc,
    .tp_new = abc_data_new,
};

static _abc_data *
_get_impl(PyObject *self)
{
    PyObject *impl = _PyObject_GetAttrId(self, &PyId__abc_impl);
    if (impl == NULL) {
        return NULL;
    }
    if (Py_TYPE(impl) != &_abc_data_type) {
        PyErr_SetString(PyExc_TypeError, "_abc_impl is set to a wrong type");
        Py_DECREF(impl);
        return NULL;
    }
    return (_abc_data *)impl;
}

static int
_in_weak_set(PyObject *set, PyObject *obj)
{
    if (set == NULL || PySet_Size(set) == 0) {
        return 0;
    }
    PyObject *ref = PyWeakref_NewRef(obj, NULL);
    if (ref == NULL) {
        if (PyErr_ExceptionMatches(PyExc_TypeError)) {
            PyErr_Clear();
            return 0;
        }
        return -1;
    }
    int res = PySet_Contains(set, ref);
    Py_DECREF(ref);
    return res;
}

static PyObject *
_destroy(PyObject *setweakref, PyObject *objweakref)
{
    PyObject *set;
    set = PyWeakref_GET_OBJECT(setweakref);
    if (set == Py_None) {
        Py_RETURN_NONE;
    }
    Py_INCREF(set);
    if (PySet_Discard(set, objweakref) < 0) {
        Py_DECREF(set);
        return NULL;
    }
    Py_DECREF(set);
    Py_RETURN_NONE;
}

static PyMethodDef _destroy_def = {
    "_destroy", (PyCFunction) _destroy, METH_O
};

static int
_add_to_weak_set(PyObject **pset, PyObject *obj)
{
    if (*pset == NULL) {
        *pset = PySet_New(NULL);
        if (*pset == NULL) {
            return -1;
        }
    }

    PyObject *set = *pset;
    PyObject *ref, *wr;
    PyObject *destroy_cb;
    wr = PyWeakref_NewRef(set, NULL);
    if (wr == NULL) {
        return -1;
    }
    destroy_cb = PyCFunction_NewEx(&_destroy_def, wr, NULL);
    ref = PyWeakref_NewRef(obj, destroy_cb);
    Py_DECREF(destroy_cb);
    if (ref == NULL) {
        Py_DECREF(wr);
        return -1;
    }
    int ret = PySet_Add(set, ref);
    Py_DECREF(wr);
    Py_DECREF(ref);
    return ret;
}


PyDoc_STRVAR(_reset_registry_doc,
"Internal ABC helper to reset registry of a given class.\n\
\n\
Should be only used by refleak.py");

static PyObject *
_reset_registry(PyObject *m, PyObject *args)
{
    PyObject *self;
    if (!PyArg_UnpackTuple(args, "_reset_registry", 1, 1, &self)) {
        return NULL;
    }
    _abc_data *impl = _get_impl(self);
    if (impl == NULL) {
        return NULL;
    }
    if (impl->_abc_registry != NULL && PySet_Clear(impl->_abc_registry) < 0) {
        Py_DECREF(impl);
        return NULL;
    }
    Py_DECREF(impl);
    Py_RETURN_NONE;
}

PyDoc_STRVAR(_reset_caches_doc,
"Internal ABC helper to reset both caches of a given class.\n\
\n\
Should be only used by refleak.py");

static PyObject *
_reset_caches(PyObject *m, PyObject *args)
{
    PyObject *self;
    _abc_data *impl;
    if (!PyArg_UnpackTuple(args, "_reset_caches", 1, 1, &self)) {
        return NULL;
    }
    impl = _get_impl(self);
    if (impl == NULL) {
        return NULL;
    }
    if (PySet_Clear(impl->_abc_cache) < 0) {
        Py_DECREF(impl);
        return NULL;
    }
    /* also the second cache */
    if (PySet_Clear(impl->_abc_negative_cache) < 0) {
        Py_DECREF(impl);
        return NULL;
    }
    Py_DECREF(impl);
    Py_RETURN_NONE;
}

PyDoc_STRVAR(_get_dump_doc,
"Internal ABC helper for cache and registry debugging.\n\
\n\
Return shallow copies of registry, of both caches, and\n\
negative cache version. Don't call this function directly,\n\
instead use ABC._dump_registry() for a nice repr.");

static PyObject *
_get_dump(PyObject *m, PyObject *args)
{
    PyObject *self, *registry, *cache, *negative_cache;
    _abc_data *impl;
    if (!PyArg_UnpackTuple(args, "_get_dump", 1, 1, &self)) {
        return NULL;
    }
    impl = _get_impl(self);
    if (impl == NULL) {
        return NULL;
    }
    registry = PySet_New(impl->_abc_registry);
    if (registry == NULL) {
        Py_DECREF(impl);
        return NULL;
    }
    cache = PySet_New(impl->_abc_cache);
    if (cache == NULL) {
        Py_DECREF(impl);
        Py_DECREF(registry);
        return NULL;
    }
    negative_cache = PySet_New(impl->_abc_negative_cache);
    if (negative_cache == NULL) {
        Py_DECREF(impl);
        Py_DECREF(registry);
        Py_DECREF(cache);
        return NULL;
    }
    Py_INCREF(impl->_abc_negative_cache_version); /* PyTuple_Packdoesn't do this. */
    PyObject *res = PyTuple_Pack(4,
            registry, cache, negative_cache, impl->_abc_negative_cache_version);
    Py_DECREF(registry);
    Py_DECREF(cache);
    Py_DECREF(negative_cache);
    Py_DECREF(impl->_abc_negative_cache_version);
    Py_DECREF(impl);
    return res;
}

// Compute set of abstract method names.
static int
compute_abstract_methods(PyObject *self)
{
    int ret = -1;
    PyObject *abstracts = PyFrozenSet_New(NULL);
    if (abstracts == NULL) {
        return -1;
    }

    PyObject *ns = NULL, *items = NULL, *bases = NULL;  // Py_XDECREF()ed on error.

    /* Stage 1: direct abstract methods. */
    ns = _PyObject_GetAttrId(self, &PyId___dict__);
    if (!ns) {
        goto error;
    }

    /* TODO: Fast path for exact dicts with PyDict_Next */
    items = PyMapping_Items(ns);
    if (!items) {
        goto error;
    }
    assert(PyList_Check(items));
    for (Py_ssize_t pos = 0; pos < PyList_GET_SIZE(items); pos++) {
        PyObject *it = PySequence_Fast(
                PyList_GET_ITEM(items, pos),
                "items() returned non-iterable");
        if (!it) {
            goto error;
        }
        if (PySequence_Fast_GET_SIZE(it) != 2) {
            PyErr_SetString(PyExc_TypeError,
                            "items() returned item which size is not 2");
            Py_DECREF(it);
            goto error;
        }

        // borrowed
        PyObject *key = PySequence_Fast_GET_ITEM(it, 0);
        PyObject *value = PySequence_Fast_GET_ITEM(it, 1);
        // items or it may be cleared while accessing __abstractmethod__
        // So we need to keep strong reference for key
        Py_INCREF(key);
        int is_abstract = _PyObject_IsAbstract(value);
        if (is_abstract < 0 ||
                (is_abstract && PySet_Add(abstracts, key) < 0)) {
            Py_DECREF(it);
            Py_DECREF(key);
            goto error;
        }
        Py_DECREF(key);
        Py_DECREF(it);
    }

    /* Stage 2: inherited abstract methods. */
    bases = _PyObject_GetAttrId(self, &PyId___bases__);
    if (!bases) {
        goto error;
    }
    if (!PyTuple_Check(bases)) {
        PyErr_SetString(PyExc_TypeError, "__bases__ is not tuple");
        goto error;
    }

    for (Py_ssize_t pos = 0; pos < PyTuple_GET_SIZE(bases); pos++) {
        PyObject *item = PyTuple_GET_ITEM(bases, pos);  // borrowed
        PyObject *base_abstracts, *iter;

        if (_PyObject_LookupAttrId(item, &PyId___abstractmethods__,
                                   &base_abstracts) < 0) {
            goto error;
        }
        if (base_abstracts == NULL) {
            continue;
        }
        if (!(iter = PyObject_GetIter(base_abstracts))) {
            Py_DECREF(base_abstracts);
            goto error;
        }
        Py_DECREF(base_abstracts);
        PyObject *key, *value;
        while ((key = PyIter_Next(iter))) {
            if (_PyObject_LookupAttr(self, key, &value) < 0) {
                Py_DECREF(key);
                Py_DECREF(iter);
                goto error;
            }
            if (value == NULL) {
                Py_DECREF(key);
                continue;
            }

            int is_abstract = _PyObject_IsAbstract(value);
            Py_DECREF(value);
            if (is_abstract < 0 ||
                    (is_abstract && PySet_Add(abstracts, key) < 0)) {
                Py_DECREF(key);
                Py_DECREF(iter);
                goto error;
            }
            Py_DECREF(key);
        }
        Py_DECREF(iter);
        if (PyErr_Occurred()) {
            goto error;
        }
    }

    if (_PyObject_SetAttrId(self, &PyId___abstractmethods__, abstracts) < 0) {
        goto error;
    }

    ret = 0;
error:
    Py_DECREF(abstracts);
    Py_XDECREF(ns);
    Py_XDECREF(items);
    Py_XDECREF(bases);
    return ret;
}

PyDoc_STRVAR(_abc_init_doc,
"Internal ABC helper for class set-up. Should be never used outside abc module");

static PyObject *
_abc_init(PyObject *m, PyObject *args)
{
    PyObject *self, *data;
    if (!PyArg_UnpackTuple(args, "_abc_init", 1, 1, &self)) {
        return NULL;
    }

    if (compute_abstract_methods(self) < 0) {
        return NULL;
    }

    /* Set up inheritance registry. */
    data = abc_data_new(&_abc_data_type, NULL, NULL);
    if (data == NULL) {
        return NULL;
    }
    if (_PyObject_SetAttrId(self, &PyId__abc_impl, data) < 0) {
        Py_DECREF(data);
        return NULL;
    }
    Py_DECREF(data);
    Py_RETURN_NONE;
}

PyDoc_STRVAR(_abc_register_doc,
"Internal ABC helper for subclasss registration. Should be never used outside abc module");

static PyObject *
_abc_register(PyObject *m, PyObject *args)
{
    PyObject *self, *subclass = NULL;
    if (!PyArg_UnpackTuple(args, "_abc_register", 2, 2, &self, &subclass)) {
        return NULL;
    }
    if (!PyType_Check(subclass)) {
        PyErr_SetString(PyExc_TypeError, "Can only register classes");
        return NULL;
    }
    int result = PyObject_IsSubclass(subclass, self);
    if (result > 0) {
        Py_INCREF(subclass);
        return subclass;  /* Already a subclass. */
    }
    if (result < 0) {
        return NULL;
    }
    /* Subtle: test for cycles *after* testing for "already a subclass";
       this means we allow X.register(X) and interpret it as a no-op. */
    result = PyObject_IsSubclass(self, subclass);
    if (result > 0) {
        /* This would create a cycle, which is bad for the algorithm below. */
        PyErr_SetString(PyExc_RuntimeError, "Refusing to create an inheritance cycle");
        return NULL;
    }
    if (result < 0) {
        return NULL;
    }
    _abc_data *impl = _get_impl(self);
    if (impl == NULL) {
        return NULL;
    }
    if (_add_to_weak_set(&impl->_abc_registry, subclass) < 0) {
        Py_DECREF(impl);
        return NULL;
    }
    Py_DECREF(impl);

    /* Invalidate negative cache */
    PyObject *one = PyLong_FromLong(1);
    if (one == NULL) {
        return NULL;
    }
    PyObject *next_version = PyNumber_Add(abc_invalidation_counter, one);
    Py_DECREF(one);
    if (next_version == NULL) {
        return NULL;
    }
    Py_SETREF(abc_invalidation_counter, next_version);

    Py_INCREF(subclass);
    return subclass;
}

PyDoc_STRVAR(_abc_instancecheck_doc,
"Internal ABC helper for instance checks. Should be never used outside abc module");

static PyObject *
_abc_instancecheck(PyObject *m, PyObject *args)
{
    PyObject *self, *result = NULL, *subclass = NULL,
             *subtype, *instance = NULL;
    if (!PyArg_UnpackTuple(args, "_abc_instancecheck", 2, 2, &self, &instance)) {
        return NULL;
    }

    _abc_data *impl = _get_impl(self);
    if (impl == NULL) {
        return NULL;
    }

    subclass = _PyObject_GetAttrId(instance, &PyId___class__);
    /* Inline the cache checking. */
    int incache = _in_weak_set(impl->_abc_cache, subclass);
    if (incache < 0) {
        goto end;
    }
    if (incache > 0) {
        result = Py_True;
        Py_INCREF(result);
        goto end;
    }
    subtype = (PyObject *)Py_TYPE(instance);
    if (subtype == subclass) {
        int r = PyObject_RichCompareBool(
            impl->_abc_negative_cache_version,
            abc_invalidation_counter, Py_EQ);
        assert(r >= 0);  // Both should be PyLong
        if (r > 0) {
            incache = _in_weak_set(impl->_abc_negative_cache, subclass);
            if (incache < 0) {
                goto end;
            }
            if (incache > 0) {
                result = Py_False;
                Py_INCREF(result);
                goto end;
            }
        }
        /* Fall back to the subclass check. */
        result = _PyObject_CallMethodIdObjArgs(self, &PyId___subclasscheck__,
                                               subclass, NULL);
        goto end;
    }
    result = _PyObject_CallMethodIdObjArgs(self, &PyId___subclasscheck__,
                                           subclass, NULL);
    if (result == NULL) {
        goto end;
    }

    switch (PyObject_IsTrue(result)) {
    case -1:
        Py_DECREF(result);
        result = NULL;
        break;
    case 0:
        Py_DECREF(result);
        result = _PyObject_CallMethodIdObjArgs(self, &PyId___subclasscheck__,
                                               subtype, NULL);
        break;
    case 1:  // Nothing to do.
        break;
    }

end:
    Py_XDECREF(impl);
    Py_XDECREF(subclass);
    return result;
}


// Return -1 when exception occured.
// Return 1 when result is set.
// Return 0 otherwise.
static int subclasscheck_check_registry(_abc_data *impl, PyObject *subclass,
                                        PyObject **result);

PyDoc_STRVAR(_abc_subclasscheck_doc,
"Internal ABC helper for subclasss checks. Should be never used outside abc module");

static PyObject *
_abc_subclasscheck(PyObject *m, PyObject *args)
{
    PyObject *self, *subclasses = NULL, *subclass = NULL, *result = NULL;
    PyObject *ok, *mro;
    Py_ssize_t pos;
    int incache;
    if (!PyArg_UnpackTuple(args, "_abc_subclasscheck", 2, 2, &self, &subclass)) {
        return NULL;
    }

    _abc_data *impl = _get_impl(self);
    if (impl == NULL) {
        return NULL;
    }

    /* 1. Check cache. */
    incache = _in_weak_set(impl->_abc_cache, subclass);
    if (incache < 0) {
        goto end;
    }
    if (incache > 0) {
        result = Py_True;
        goto end;
    }

    /* 2. Check negative cache; may have to invalidate. */
    int r = PyObject_RichCompareBool(impl->_abc_negative_cache_version,
                                     abc_invalidation_counter, Py_LT);
    assert(r >= 0);  // Both should be PyLong
    if (r > 0) {
        /* Invalidate the negative cache. */
        if (impl->_abc_negative_cache != NULL &&
                PySet_Clear(impl->_abc_negative_cache) < 0) {
            goto end;
        }
        /* INCREF the new value of cache version,
           then carefully DECREF the old one. */
        Py_INCREF(abc_invalidation_counter);
        Py_SETREF(impl->_abc_negative_cache_version, abc_invalidation_counter);
    }
    else {
        incache = _in_weak_set(impl->_abc_negative_cache, subclass);
        if (incache < 0) {
            goto end;
        }
        if (incache > 0) {
            result = Py_False;
            goto end;
        }
    }

    /* 3. Check the subclass hook. */
    ok = _PyObject_CallMethodIdObjArgs((PyObject *)self, &PyId___subclasshook__,
                                       subclass, NULL);
    if (ok == NULL) {
        goto end;
    }
    if (ok == Py_True) {
        Py_DECREF(ok);
        if (_add_to_weak_set(&impl->_abc_cache, subclass) < 0) {
            goto end;
        }
        result = Py_True;
        goto end;
    }
    if (ok == Py_False) {
        Py_DECREF(ok);
        if (_add_to_weak_set(&impl->_abc_negative_cache, subclass) < 0) {
            goto end;
        }
        result = Py_False;
        goto end;
    }
    if (ok != Py_NotImplemented) {
        Py_DECREF(ok);
        PyErr_SetString(PyExc_AssertionError, "__subclasshook__ must return either"
                                              " False, True, or NotImplemented");
        goto end;
    }
    Py_DECREF(ok);

    /* 4. Check if it's a direct subclass. */
    mro = ((PyTypeObject *)subclass)->tp_mro;
    assert(PyTuple_Check(mro));
    for (pos = 0; pos < PyTuple_Size(mro); pos++) {
        PyObject *mro_item = PyTuple_GetItem(mro, pos);
        if (mro_item == NULL) {
            goto end;
        }
        if ((PyObject *)self == mro_item) {
            if (_add_to_weak_set(&impl->_abc_cache, subclass) < 0) {
                goto end;
            }
            result = Py_True;
            goto end;
        }
    }

    /* 5. Check if it's a subclass of a registered class (recursive). */
    if (subclasscheck_check_registry(impl, subclass, &result)) {
        // Exception occured or result is set.
        goto end;
    }

    /* 6. Check if it's a subclass of a subclass (recursive). */
    subclasses = PyObject_CallMethod(self, "__subclasses__", NULL);
    if(!PyList_Check(subclasses)) {
        PyErr_SetString(PyExc_TypeError, "__subclasses__() must return a list");
        goto end;
    }
    for (pos = 0; pos < PyList_GET_SIZE(subclasses); pos++) {
        int r = PyObject_IsSubclass(subclass, PyList_GET_ITEM(subclasses, pos));
        if (r > 0) {
            if (_add_to_weak_set(&impl->_abc_cache, subclass) < 0) {
                goto end;
            }
            result = Py_True;
            goto end;
        }
        if (r < 0) {
            goto end;
        }
    }

    /* No dice; update negative cache. */
    if (_add_to_weak_set(&impl->_abc_negative_cache, subclass) < 0) {
        goto end;
    }
    result = Py_False;

end:
    Py_XDECREF(impl);
    Py_XDECREF(subclasses);
    Py_XINCREF(result);
    return result;
}


static int
subclasscheck_check_registry(_abc_data *impl, PyObject *subclass,
                             PyObject **result)
{
    // Fast path: check subclass is in weakref directly.
    int ret = _in_weak_set(impl->_abc_registry, subclass);
    if (ret < 0) {
        *result = NULL;
        return -1;
    }
    if (ret > 0) {
        *result = Py_True;
        return 1;
    }

    if (impl->_abc_registry == NULL) {
        return 0;
    }
    Py_ssize_t registry_size = PySet_Size(impl->_abc_registry);
    if (registry_size == 0) {
        return 0;
    }
    // Weakref callback may remove entry from set.
    // Se we take snapshot of registry first.
    PyObject **copy = PyMem_Malloc(sizeof(PyObject*) * registry_size);
    PyObject *key;
    Py_ssize_t pos = 0;
    Py_hash_t hash;
    Py_ssize_t i = 0;

    while (_PySet_NextEntry(impl->_abc_registry, &pos, &key, &hash)) {
        Py_INCREF(key);
        copy[i++] = key;
    }
    assert(i == registry_size);

    for (i = 0; i < registry_size; i++) {
        PyObject *rkey = PyWeakref_GetObject(copy[i]);
        if (rkey == NULL) {
            // Someone inject non-weakref type in the registry.
            ret = -1;
            break;
        }
        if (rkey == Py_None) {
            continue;
        }
        Py_INCREF(rkey);
        int r = PyObject_IsSubclass(subclass, rkey);
        Py_DECREF(rkey);
        if (r < 0) {
            ret = -1;
            break;
        }
        if (r > 0) {
            if (_add_to_weak_set(&impl->_abc_cache, subclass) < 0) {
                ret = -1;
                break;
            }
            *result = Py_True;
            ret = 1;
            break;
        }
    }

    for (i = 0; i < registry_size; i++) {
        Py_DECREF(copy[i]);
    }
    PyMem_Free(copy);
    return ret;
}


PyDoc_STRVAR(_cache_token_doc,
"Returns the current ABC cache token.\n\
\n\
The token is an opaque object (supporting equality testing) identifying the\n\
current version of the ABC cache for virtual subclasses. The token changes\n\
with every call to ``register()`` on any ABC.");

static PyObject *
get_cache_token(void)
{
    Py_INCREF(abc_invalidation_counter);
    return abc_invalidation_counter;
}

static struct PyMethodDef module_functions[] = {
    {"get_cache_token", (PyCFunction)get_cache_token, METH_NOARGS,
        _cache_token_doc},
    {"_abc_init", (PyCFunction)_abc_init, METH_VARARGS,
        _abc_init_doc},
    {"_reset_registry", (PyCFunction)_reset_registry, METH_VARARGS,
        _reset_registry_doc},
    {"_reset_caches", (PyCFunction)_reset_caches, METH_VARARGS,
        _reset_caches_doc},
    {"_get_dump", (PyCFunction)_get_dump, METH_VARARGS,
        _get_dump_doc},
    {"_abc_register", (PyCFunction)_abc_register, METH_VARARGS,
        _abc_register_doc},
    {"_abc_instancecheck", (PyCFunction)_abc_instancecheck, METH_VARARGS,
        _abc_instancecheck_doc},
    {"_abc_subclasscheck", (PyCFunction)_abc_subclasscheck, METH_VARARGS,
        _abc_subclasscheck_doc},
    {NULL,       NULL}          /* sentinel */
};

static struct PyModuleDef _abcmodule = {
    PyModuleDef_HEAD_INIT,
    "_abc",
    _abc__doc__,
    -1,
    module_functions,
    NULL,
    NULL,
    NULL,
    NULL
};


PyMODINIT_FUNC
PyInit__abc(void)
{
    if (PyType_Ready(&_abc_data_type) < 0) {
        return NULL;
    }
    _abc_data_type.tp_doc = abc_data_doc;

    abc_invalidation_counter = PyLong_FromLong(0);
    return PyModule_Create(&_abcmodule);
}
