/* ABCMeta implementation */

/* TODO: Global check where checks are needed, and where I made objects myself */
/* In particular use capitals like PyList_GET_SIZE */
/* Think (ask) about inlining some calls, like __subclasses__ */
/* Use PyId instead of string attrs */

#include "Python.h"
#include "structmember.h"

PyDoc_STRVAR(_abc__doc__,
"Module contains faster C implementation of abc.ABCMeta");
#define DEFERRED_ADDRESS(ADDR) 0

_Py_IDENTIFIER(stdout);

static Py_ssize_t abc_invalidation_counter = 0;

typedef struct {
    PyHeapTypeObject tp;
    PyObject *abc_registry; /* normal set of weakrefs without callback */
    PyObject *abc_cache;          /* normal set of weakrefs with callback (we never iterate over it) */
    PyObject *abc_negative_cache; /* normal set of weakrefs with callback */
    Py_ssize_t abc_negative_cache_version;
} abc;

static void
abcmeta_dealloc(abc *tp)
{
    Py_CLEAR(tp->abc_registry);
    Py_CLEAR(tp->abc_cache);
    Py_CLEAR(tp->abc_negative_cache);
    PyType_Type.tp_dealloc((PyObject *)tp);
}

static int
abcmeta_traverse(PyObject *self, visitproc visit, void *arg)
{
    Py_VISIT(((abc *)self)->abc_registry);
    Py_VISIT(((abc *)self)->abc_cache);
    Py_VISIT(((abc *)self)->abc_negative_cache);
    return PyType_Type.tp_traverse(self, visit, arg);
}

static int
abcmeta_clear(abc *tp)
{
    PySet_Clear(tp->abc_registry);
    PySet_Clear(tp->abc_cache);
    PySet_Clear(tp->abc_negative_cache);
    return PyType_Type.tp_clear((PyObject *)tp);
}

static PyObject *
abcmeta_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    abc *result = NULL;
    PyObject *ns, *bases, *items, *abstracts, *is_abstract, *base_abstracts;
    PyObject *key, *value, *item, *iter;
    Py_ssize_t pos = 0;

    result = (abc *)PyType_Type.tp_new(type, args, kwds);
    if (!result) {
        return NULL;
    }
    result->abc_registry = PySet_New(NULL); /* TODO: Delay registry creation until it is actually needed */
    result->abc_cache = PySet_New(NULL);
    result->abc_negative_cache = PySet_New(NULL);
    if (!result->abc_registry || !result->abc_cache ||
        !result->abc_negative_cache) {
        return NULL;
    }
    result->abc_negative_cache_version = abc_invalidation_counter;
    abstracts = PyFrozenSet_New(NULL);
    /* Stage 1: direct abstract methods */
    /* Safe to assume everything is fine since type.__new__ succeeded */
    ns = PyTuple_GET_ITEM(args, 2);
    items = PyMapping_Items(ns); /* TODO: Fast path for exact dicts with PyDict_Next */
    for (pos = 0; pos < PySequence_Size(items); pos++) { /* TODO: Check if it is a list or tuple? */
        item = PySequence_GetItem(items, pos);
        key = PyTuple_GetItem(item, 0);
        value = PyTuple_GetItem(item, 1);
        is_abstract = PyObject_GetAttrString(value, "__isabstractmethod__");
        if (!is_abstract) {
            if (!PyErr_ExceptionMatches(PyExc_AttributeError)) {
                return NULL;
            }
            PyErr_Clear();
            continue;
        }
        if (is_abstract == Py_True && PySet_Add(abstracts, key) < 0) {
            return NULL;
        }
    }
    /* Stage 2: inherited abstract methods */
    bases = PyTuple_GET_ITEM(args, 1);
    for (pos = 0; pos < PyTuple_Size(bases); pos++) {
        item = PyTuple_GetItem(bases, pos);
        base_abstracts = PyObject_GetAttrString(item, "__abstractmethods__");
        if (!base_abstracts) {
            if (!PyErr_ExceptionMatches(PyExc_AttributeError)) {
                return NULL;
            }
            PyErr_Clear();
            continue;
        }
        if (!(iter = PyObject_GetIter(base_abstracts))) {
            return NULL;
        }
        while ((key = PyIter_Next(iter))) {
            value = PyObject_GetAttr((PyObject *)result, key);
            if (!value) {
                if (!PyErr_ExceptionMatches(PyExc_AttributeError)) {
                    return NULL;
                }
                PyErr_Clear();
                continue;
            }
            is_abstract = PyObject_GetAttrString(value, "__isabstractmethod__");
            if (!is_abstract) {
                if (!PyErr_ExceptionMatches(PyExc_AttributeError)) {
                    return NULL;
                }
                PyErr_Clear();
                continue;
            }
            if (is_abstract == Py_True && PySet_Add(abstracts, key) < 0) {
                return NULL;
            }
            Py_DECREF(key);
        }
        Py_DECREF(iter);
    }
    if (PyObject_SetAttrString((PyObject *)result, "__abstractmethods__", abstracts) < 0) {
        return NULL;
    }
    return (PyObject *)result;
}

static PyObject *
abcmeta_register(abc *self, PyObject *args)
{
    PyObject *subclass = NULL;
    if (!PyArg_UnpackTuple(args, "register", 1, 1, &subclass)) {
        return NULL;
    }
    if (!PyType_Check(subclass)) {
        PyErr_SetString(PyExc_TypeError, "Can only register classes");
        return NULL;
    }
    if (PyObject_IsSubclass(subclass, (PyObject *)self)) { /* TODO: Check for error here */
        Py_INCREF(subclass);
        return subclass;
    }
    if (PyObject_IsSubclass((PyObject *)self, subclass)) { /* TODO: Check for error here */
        PyErr_SetString(PyExc_RuntimeError, "Refusing to create an inheritance cycle");
        return NULL;
    }
    if (PySet_Add(self->abc_registry, subclass) < 0) {
        return NULL;
    }
    Py_INCREF(subclass);
    abc_invalidation_counter++;
    return subclass;
}

static PyObject *
abcmeta_subclasscheck(abc *self, PyObject *args); /* Forward */

static PyObject *
abcmeta_instancecheck(abc *self, PyObject *args)
{
    PyObject *subclass, *instance = NULL;
    if (!PyArg_UnpackTuple(args, "__isinstance__", 1, 1, &instance)) {
        return NULL;
    }
    subclass = (PyObject *)Py_TYPE(instance);
    /* TODO: Use cache */
    if (abcmeta_subclasscheck(self, PyTuple_Pack(1, subclass)) == Py_True) { /* TODO: Refactor to avoid packing */
        return Py_True;
    }
    subclass = PyObject_GetAttrString(instance, "__class__");
    return abcmeta_subclasscheck(self, PyTuple_Pack(1, subclass));
}

static PyObject *
abcmeta_subclasscheck(abc *self, PyObject *args)
{
    PyObject *subclasses, *subclass = NULL;
    PyObject *ok, *mro, *iter, *key;
    Py_ssize_t pos;
    if (!PyArg_UnpackTuple(args, "__issubclass__", 1, 1, &subclass)) {
        return NULL;
    }
    /* TODO: clear the registry from dead refs from time to time
       on iteration here (have a counter for this) */
    /* TODO: Reset caches every n-th succes/failure correspondingly
       so that they don't grow too large */
    ok = PyObject_CallMethod((PyObject *)self, "__subclasshook__", "O", subclass);
    if (ok == Py_True) {
        Py_INCREF(Py_True);
        return Py_True;
    }
    if (ok == Py_False) {
        Py_INCREF(Py_False);
        return Py_False;
    }
    mro = ((PyTypeObject *)subclass)->tp_mro;
    for (pos = 0; pos < PyTuple_Size(mro); pos++) {
        if ((PyObject *)self == PyTuple_GetItem(mro, pos)) {
            Py_INCREF(Py_True);
            return Py_True;
        }
    }
    iter = PyObject_GetIter(self->abc_registry);
    while ((key = PyIter_Next(iter))) {
        if (PyObject_IsSubclass(subclass, key)) {
            Py_INCREF(Py_True);
            return Py_True;
        }
        Py_DECREF(key);
    }
    Py_DECREF(iter);
    subclasses = PyObject_CallMethod((PyObject *)self, "__subclasses__", NULL);
    for (pos = 0; pos < PyList_GET_SIZE(subclasses); pos++) {
        if (PyObject_IsSubclass(subclass, PyList_GET_ITEM(subclasses, pos))) {
            Py_INCREF(Py_True);
            return Py_True;
        }
    }
    Py_INCREF(Py_False);
    return Py_False;
}

int
_print_message(PyObject *file, const char* message)
{
    PyObject *mo = PyUnicode_FromString(message);
    if (!mo) {
        return -1;
    }
    if (PyFile_WriteObject(mo, file, Py_PRINT_RAW)) {
        return -1;
    }
    return 0;
}

static PyObject *
abcmeta_dump(abc *self, PyObject *args)
{
    PyObject *file = NULL;
    PyObject *sizeo, *version = PyLong_FromSsize_t(self->abc_negative_cache_version);
    Py_ssize_t size;
    if (!PyArg_UnpackTuple(args, "_dump_registry", 0, 1, &file)) {
        return NULL;
    }
    if (!version) {
        return NULL;
    }
    if (file == NULL || file == Py_None) {
        file = _PySys_GetObjectId(&PyId_stdout);
        if (file == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "lost sys.stdout");
            return NULL;
        }
    }
    /* Header */
    if (_print_message(file, "Class: ")) {
        return NULL;
    }
    if (_print_message(file, ((PyTypeObject *)self)->tp_name)) {
        return NULL;
    }
    if (_print_message(file, "\n")) {
        return NULL;
    }
    /* Registry */
    if (_print_message(file, "Registry: ")) {
        return NULL;
    }
    if (PyFile_WriteObject(self->abc_registry, file, Py_PRINT_RAW)) {
        return NULL;
    }
    if (_print_message(file, "\n")) {
        return NULL;
    }
    /* Postive cahce */
    if (_print_message(file, "Positive cache: ")) {
        return NULL;
    }
    size = PySet_GET_SIZE(self->abc_cache);
    if (!(sizeo = PyLong_FromSsize_t(size))) {
        return NULL;
    }
    if (PyFile_WriteObject(sizeo, file, Py_PRINT_RAW)) {
        return NULL;
    }
    if (_print_message(file, " items\n")) {
        return NULL;
    }
    /* Negative cahce */
    if (_print_message(file, "Negative cache: ")) {
        return NULL;
    }
    size = PySet_GET_SIZE(self->abc_cache);
    if (!(sizeo = PyLong_FromSsize_t(size))) {
        return NULL;
    }
    if (PyFile_WriteObject(sizeo, file, Py_PRINT_RAW)) {
        return NULL;
    }
    if (_print_message(file, " items\n")) {
        return NULL;
    }
    if (_print_message(file, "Negative cache version: ")) {
        return NULL;
    }
    if (PyFile_WriteObject(version, file, Py_PRINT_RAW)) {
        return NULL;
    }
    if (_print_message(file, "\n")) {
        return NULL;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

PyDoc_STRVAR(_register_doc,
"Register a virtual subclass of an ABC.\n\
\n\
Returns the subclass, to allow usage as a class decorator.");

static PyMethodDef abcmeta_methods[] = {
    {"register", (PyCFunction)abcmeta_register, METH_VARARGS,
        _register_doc},
    {"__instancecheck__", (PyCFunction)abcmeta_instancecheck, METH_VARARGS,
        PyDoc_STR("Override for isinstance(instance, cls).")},
    {"__subclasscheck__", (PyCFunction)abcmeta_subclasscheck, METH_VARARGS,
        PyDoc_STR("Override for issubclass(subclass, cls).")},
    {"_dump_registry", (PyCFunction)abcmeta_dump, METH_VARARGS,
        PyDoc_STR("Debug helper to print the ABC registry.")},
    {NULL,      NULL},
};

PyDoc_STRVAR(abcmeta_doc,
 "Metaclass for defining Abstract Base Classes (ABCs).\n\
\n\
Use this metaclass to create an ABC.  An ABC can be subclassed\n\
directly, and then acts as a mix-in class.  You can also register\n\
unrelated concrete classes (even built-in classes) and unrelated\n\
ABCs as 'virtual subclasses' -- these and their descendants will\n\
be considered subclasses of the registering ABC by the built-in\n\
issubclass() function, but the registering ABC won't show up in\n\
their MRO (Method Resolution Order) nor will method\n\
implementations defined by the registering ABC be callable (not\n\
even via super()).");

PyTypeObject ABCMeta = {
    PyVarObject_HEAD_INIT(DEFERRED_ADDRESS(&PyType_Type), 0)
    "ABCMeta",                                  /* tp_name */
    sizeof(abc),                                /* tp_basicsize */
    0,                                          /* tp_itemsize */
    (destructor)abcmeta_dealloc,                /* tp_dealloc */
    0,                                          /* tp_print */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_reserved */
    0,                                          /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    0,                                          /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_HAVE_VERSION_TAG |
        Py_TPFLAGS_BASETYPE | Py_TPFLAGS_TYPE_SUBCLASS,         /* tp_flags */
    abcmeta_doc,                                /* tp_doc */
    abcmeta_traverse,                           /* tp_traverse */
    (inquiry)abcmeta_clear,                     /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    0,                                          /* tp_iter */
    0,                                          /* tp_iternext */
    abcmeta_methods,                            /* tp_methods */
    0,                                          /* tp_members */
    0,                                          /* tp_getset */
    DEFERRED_ADDRESS(&PyType_Type),             /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    0,                                          /* tp_init */
    0,                                          /* tp_alloc */
    abcmeta_new,                                /* tp_new */
    0,                                          /* tp_free */
    0,                                          /* tp_is_gc */
};

PyDoc_STRVAR(_cache_token_doc,
"Returns the current ABC cache token.\n\
\n\
The token is an opaque object (supporting equality testing) identifying the\n\
current version of the ABC cache for virtual subclasses. The token changes\n\
with every call to ``register()`` on any ABC.");

static PyObject *
get_cache_token(void)
{
    return PyLong_FromSsize_t(abc_invalidation_counter);
}

static struct PyMethodDef module_functions[] = {
    {"get_cache_token", get_cache_token, METH_NOARGS, _cache_token_doc},
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
    PyObject *m;

    m = PyModule_Create(&_abcmodule);
    if (m == NULL)
        return NULL;
    ABCMeta.tp_base = &PyType_Type;
    if (PyType_Ready(&ABCMeta) < 0) {
        return NULL;
    }
    Py_INCREF(&ABCMeta);
    if (PyModule_AddObject(m, "ABCMeta",
                           (PyObject *) &ABCMeta) < 0) {
        return NULL;
    }
    return m;
}
