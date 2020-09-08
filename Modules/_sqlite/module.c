/* module.c - the module itself
 *
 * Copyright (C) 2004-2010 Gerhard Häring <gh@ghaering.de>
 *
 * This file is part of pysqlite.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#include "connection.h"
#include "statement.h"
#include "cursor.h"
#include "cache.h"
#include "prepare_protocol.h"
#include "microprotocols.h"
#include "row.h"

#if SQLITE_VERSION_NUMBER < 3007003
#error "SQLite 3.7.3 or higher required"
#endif

#include "clinic/module.c.h"
/*[clinic input]
module _sqlite3
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=81e330492d57488e]*/

/* static objects at module-level */

PyObject *pysqlite_Error = NULL;
PyObject *pysqlite_Warning = NULL;
PyObject *pysqlite_InterfaceError = NULL;
PyObject *pysqlite_DatabaseError = NULL;
PyObject *pysqlite_InternalError = NULL;
PyObject *pysqlite_OperationalError = NULL;
PyObject *pysqlite_ProgrammingError = NULL;
PyObject *pysqlite_IntegrityError = NULL;
PyObject *pysqlite_DataError = NULL;
PyObject *pysqlite_NotSupportedError = NULL;

PyObject* _pysqlite_converters = NULL;
int _pysqlite_enable_callback_tracebacks = 0;
int pysqlite_BaseTypeAdapted = 0;

static PyObject* module_connect(PyObject* self, PyObject* args, PyObject*
        kwargs)
{
    /* Python seems to have no way of extracting a single keyword-arg at
     * C-level, so this code is redundant with the one in connection_init in
     * connection.c and must always be copied from there ... */

    static char *kwlist[] = {
        "database", "timeout", "detect_types", "isolation_level",
        "check_same_thread", "factory", "cached_statements", "uri",
        NULL
    };
    PyObject* database;
    int detect_types = 0;
    PyObject* isolation_level;
    PyObject* factory = NULL;
    int check_same_thread = 1;
    int cached_statements;
    int uri = 0;
    double timeout = 5.0;

    PyObject* result;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|diOiOip", kwlist,
                                     &database, &timeout, &detect_types,
                                     &isolation_level, &check_same_thread,
                                     &factory, &cached_statements, &uri))
    {
        return NULL;
    }

    if (factory == NULL) {
        factory = (PyObject*)&pysqlite_ConnectionType;
    }

    if (PySys_Audit("sqlite3.connect", "O", database) < 0) {
        return NULL;
    }

    result = PyObject_Call(factory, args, kwargs);

    return result;
}

PyDoc_STRVAR(module_connect_doc,
"connect(database[, timeout, detect_types, isolation_level,\n\
        check_same_thread, factory, cached_statements, uri])\n\
\n\
Opens a connection to the SQLite database file *database*. You can use\n\
\":memory:\" to open a database connection to a database that resides in\n\
RAM instead of on disk.");

/*[clinic input]
_sqlite3.complete_statement as pysqlite_complete_statement

    statement: str
    /

Checks if a string contains a complete SQL statement. Non-standard.
[clinic start generated code]*/

static PyObject *
pysqlite_complete_statement_impl(PyObject *module, const char *statement)
/*[clinic end generated code: output=e55f1ff1952df558 input=b15b778a9c1b557b]*/
{
    PyObject* result;

    if (sqlite3_complete(statement)) {
        result = Py_True;
    } else {
        result = Py_False;
    }

    Py_INCREF(result);

    return result;
}

/*[clinic input]
_sqlite3.enable_shared_cache as pysqlite_enable_shared_cache

    enable as do_enable: int
    /

Enable or disable shared cache mode for the calling thread.

Experimental/Non-standard.
[clinic start generated code]*/

static PyObject *
pysqlite_enable_shared_cache_impl(PyObject *module, int do_enable)
/*[clinic end generated code: output=259c74eedee1516b input=6c608c515bd7caba]*/
{
    int rc;

    rc = sqlite3_enable_shared_cache(do_enable);

    if (rc != SQLITE_OK) {
        PyErr_SetString(pysqlite_OperationalError, "Changing the shared_cache flag failed");
        return NULL;
    } else {
        Py_RETURN_NONE;
    }
}

/*[clinic input]
_sqlite3.register_adapter as pysqlite_register_adapter

    type: object(type='PyTypeObject *')
    caster: object
    /

Registers an adapter with pysqlite's adapter registry. Non-standard.
[clinic start generated code]*/

static PyObject *
pysqlite_register_adapter_impl(PyObject *module, PyTypeObject *type,
                               PyObject *caster)
/*[clinic end generated code: output=a287e8db18e8af23 input=839dad90e2492725]*/
{
    int rc;

    /* a basic type is adapted; there's a performance optimization if that's not the case
     * (99 % of all usages) */
    if (type == &PyLong_Type || type == &PyFloat_Type
            || type == &PyUnicode_Type || type == &PyByteArray_Type) {
        pysqlite_BaseTypeAdapted = 1;
    }

    rc = pysqlite_microprotocols_add(type, (PyObject*)&pysqlite_PrepareProtocolType, caster);
    if (rc == -1)
        return NULL;

    Py_RETURN_NONE;
}

/*[clinic input]
_sqlite3.register_converter as pysqlite_register_converter

    name as orig_name: unicode
    converter as callable: object
    /

Registers a converter with pysqlite. Non-standard.
[clinic start generated code]*/

static PyObject *
pysqlite_register_converter_impl(PyObject *module, PyObject *orig_name,
                                 PyObject *callable)
/*[clinic end generated code: output=a2f2bfeed7230062 input=e074cf7f4890544f]*/
{
    PyObject* name = NULL;
    PyObject* retval = NULL;
    _Py_IDENTIFIER(upper);

    /* convert the name to upper case */
    name = _PyObject_CallMethodIdNoArgs(orig_name, &PyId_upper);
    if (!name) {
        goto error;
    }

    if (PyDict_SetItem(_pysqlite_converters, name, callable) != 0) {
        goto error;
    }

    Py_INCREF(Py_None);
    retval = Py_None;
error:
    Py_XDECREF(name);
    return retval;
}

/*[clinic input]
_sqlite3.enable_callback_tracebacks as pysqlite_enable_callback_trace

    enable: int
    /

Enable or disable callback functions throwing errors to stderr.
[clinic start generated code]*/

static PyObject *
pysqlite_enable_callback_trace_impl(PyObject *module, int enable)
/*[clinic end generated code: output=4ff1d051c698f194 input=cb79d3581eb77c40]*/
{
    _pysqlite_enable_callback_tracebacks = enable;

    Py_RETURN_NONE;
}

/*[clinic input]
_sqlite3.adapt as pysqlite_adapt

    obj: object
    proto: object(c_default='(PyObject*)&pysqlite_PrepareProtocolType') = PrepareProtocolType
    alt: object = NULL
    /

Adapt given object to given protocol. Non-standard.
[clinic start generated code]*/

static PyObject *
pysqlite_adapt_impl(PyObject *module, PyObject *obj, PyObject *proto,
                    PyObject *alt)
/*[clinic end generated code: output=0c3927c5fcd23dd9 input=37bc55a5a6ee407c]*/
{
    return pysqlite_microprotocols_adapt(obj, proto, alt);
}

static void converters_init(PyObject* dict)
{
    _pysqlite_converters = PyDict_New();
    if (!_pysqlite_converters) {
        return;
    }

    PyDict_SetItemString(dict, "converters", _pysqlite_converters);
}

static PyMethodDef module_methods[] = {
    {"connect",  (PyCFunction)(void(*)(void))module_connect,
     METH_VARARGS | METH_KEYWORDS, module_connect_doc},
    PYSQLITE_ADAPT_METHODDEF
    PYSQLITE_COMPLETE_STATEMENT_METHODDEF
    PYSQLITE_ENABLE_CALLBACK_TRACE_METHODDEF
    PYSQLITE_ENABLE_SHARED_CACHE_METHODDEF
    PYSQLITE_REGISTER_ADAPTER_METHODDEF
    PYSQLITE_REGISTER_CONVERTER_METHODDEF
    {NULL, NULL}
};

struct _IntConstantPair {
    const char *constant_name;
    int constant_value;
};

typedef struct _IntConstantPair IntConstantPair;

static const IntConstantPair _int_constants[] = {
    {"PARSE_DECLTYPES", PARSE_DECLTYPES},
    {"PARSE_COLNAMES", PARSE_COLNAMES},

    {"SQLITE_OK", SQLITE_OK},
    {"SQLITE_DENY", SQLITE_DENY},
    {"SQLITE_IGNORE", SQLITE_IGNORE},
    {"SQLITE_CREATE_INDEX", SQLITE_CREATE_INDEX},
    {"SQLITE_CREATE_TABLE", SQLITE_CREATE_TABLE},
    {"SQLITE_CREATE_TEMP_INDEX", SQLITE_CREATE_TEMP_INDEX},
    {"SQLITE_CREATE_TEMP_TABLE", SQLITE_CREATE_TEMP_TABLE},
    {"SQLITE_CREATE_TEMP_TRIGGER", SQLITE_CREATE_TEMP_TRIGGER},
    {"SQLITE_CREATE_TEMP_VIEW", SQLITE_CREATE_TEMP_VIEW},
    {"SQLITE_CREATE_TRIGGER", SQLITE_CREATE_TRIGGER},
    {"SQLITE_CREATE_VIEW", SQLITE_CREATE_VIEW},
    {"SQLITE_DELETE", SQLITE_DELETE},
    {"SQLITE_DROP_INDEX", SQLITE_DROP_INDEX},
    {"SQLITE_DROP_TABLE", SQLITE_DROP_TABLE},
    {"SQLITE_DROP_TEMP_INDEX", SQLITE_DROP_TEMP_INDEX},
    {"SQLITE_DROP_TEMP_TABLE", SQLITE_DROP_TEMP_TABLE},
    {"SQLITE_DROP_TEMP_TRIGGER", SQLITE_DROP_TEMP_TRIGGER},
    {"SQLITE_DROP_TEMP_VIEW", SQLITE_DROP_TEMP_VIEW},
    {"SQLITE_DROP_TRIGGER", SQLITE_DROP_TRIGGER},
    {"SQLITE_DROP_VIEW", SQLITE_DROP_VIEW},
    {"SQLITE_INSERT", SQLITE_INSERT},
    {"SQLITE_PRAGMA", SQLITE_PRAGMA},
    {"SQLITE_READ", SQLITE_READ},
    {"SQLITE_SELECT", SQLITE_SELECT},
    {"SQLITE_TRANSACTION", SQLITE_TRANSACTION},
    {"SQLITE_UPDATE", SQLITE_UPDATE},
    {"SQLITE_ATTACH", SQLITE_ATTACH},
    {"SQLITE_DETACH", SQLITE_DETACH},
    {"SQLITE_ALTER_TABLE", SQLITE_ALTER_TABLE},
    {"SQLITE_REINDEX", SQLITE_REINDEX},
    {"SQLITE_ANALYZE", SQLITE_ANALYZE},
    {"SQLITE_CREATE_VTABLE", SQLITE_CREATE_VTABLE},
    {"SQLITE_DROP_VTABLE", SQLITE_DROP_VTABLE},
    {"SQLITE_FUNCTION", SQLITE_FUNCTION},
    {"SQLITE_SAVEPOINT", SQLITE_SAVEPOINT},
#if SQLITE_VERSION_NUMBER >= 3008003
    {"SQLITE_RECURSIVE", SQLITE_RECURSIVE},
#endif
    {"SQLITE_DONE", SQLITE_DONE},
    {(char*)NULL, 0}
};


static struct PyModuleDef _sqlite3module = {
        PyModuleDef_HEAD_INIT,
        "_sqlite3",
        NULL,
        -1,
        module_methods,
        NULL,
        NULL,
        NULL,
        NULL
};

#define ADD_TYPE(module, type)                 \
do {                                           \
    if (PyModule_AddType(module, &type) < 0) { \
        Py_DECREF(module);                     \
        return NULL;                           \
    }                                          \
} while (0)

PyMODINIT_FUNC PyInit__sqlite3(void)
{
    PyObject *module, *dict;
    PyObject *tmp_obj;
    int i;

    if (sqlite3_libversion_number() < 3007003) {
        PyErr_SetString(PyExc_ImportError, MODULE_NAME ": SQLite 3.7.3 or higher required");
        return NULL;
    }

    module = PyModule_Create(&_sqlite3module);

    if (!module ||
        (pysqlite_row_setup_types() < 0) ||
        (pysqlite_cursor_setup_types() < 0) ||
        (pysqlite_connection_setup_types() < 0) ||
        (pysqlite_cache_setup_types() < 0) ||
        (pysqlite_statement_setup_types() < 0) ||
        (pysqlite_prepare_protocol_setup_types() < 0)
       ) {
        Py_XDECREF(module);
        return NULL;
    }

    ADD_TYPE(module, pysqlite_ConnectionType);
    ADD_TYPE(module, pysqlite_CursorType);
    ADD_TYPE(module, pysqlite_PrepareProtocolType);
    ADD_TYPE(module, pysqlite_RowType);

    if (!(dict = PyModule_GetDict(module))) {
        goto error;
    }

    /*** Create DB-API Exception hierarchy */

    if (!(pysqlite_Error = PyErr_NewException(MODULE_NAME ".Error", PyExc_Exception, NULL))) {
        goto error;
    }
    PyDict_SetItemString(dict, "Error", pysqlite_Error);

    if (!(pysqlite_Warning = PyErr_NewException(MODULE_NAME ".Warning", PyExc_Exception, NULL))) {
        goto error;
    }
    PyDict_SetItemString(dict, "Warning", pysqlite_Warning);

    /* Error subclasses */

    if (!(pysqlite_InterfaceError = PyErr_NewException(MODULE_NAME ".InterfaceError", pysqlite_Error, NULL))) {
        goto error;
    }
    PyDict_SetItemString(dict, "InterfaceError", pysqlite_InterfaceError);

    if (!(pysqlite_DatabaseError = PyErr_NewException(MODULE_NAME ".DatabaseError", pysqlite_Error, NULL))) {
        goto error;
    }
    PyDict_SetItemString(dict, "DatabaseError", pysqlite_DatabaseError);

    /* pysqlite_DatabaseError subclasses */

    if (!(pysqlite_InternalError = PyErr_NewException(MODULE_NAME ".InternalError", pysqlite_DatabaseError, NULL))) {
        goto error;
    }
    PyDict_SetItemString(dict, "InternalError", pysqlite_InternalError);

    if (!(pysqlite_OperationalError = PyErr_NewException(MODULE_NAME ".OperationalError", pysqlite_DatabaseError, NULL))) {
        goto error;
    }
    PyDict_SetItemString(dict, "OperationalError", pysqlite_OperationalError);

    if (!(pysqlite_ProgrammingError = PyErr_NewException(MODULE_NAME ".ProgrammingError", pysqlite_DatabaseError, NULL))) {
        goto error;
    }
    PyDict_SetItemString(dict, "ProgrammingError", pysqlite_ProgrammingError);

    if (!(pysqlite_IntegrityError = PyErr_NewException(MODULE_NAME ".IntegrityError", pysqlite_DatabaseError,NULL))) {
        goto error;
    }
    PyDict_SetItemString(dict, "IntegrityError", pysqlite_IntegrityError);

    if (!(pysqlite_DataError = PyErr_NewException(MODULE_NAME ".DataError", pysqlite_DatabaseError, NULL))) {
        goto error;
    }
    PyDict_SetItemString(dict, "DataError", pysqlite_DataError);

    if (!(pysqlite_NotSupportedError = PyErr_NewException(MODULE_NAME ".NotSupportedError", pysqlite_DatabaseError, NULL))) {
        goto error;
    }
    PyDict_SetItemString(dict, "NotSupportedError", pysqlite_NotSupportedError);

    /* In Python 2.x, setting Connection.text_factory to
       OptimizedUnicode caused Unicode objects to be returned for
       non-ASCII data and bytestrings to be returned for ASCII data.
       Now OptimizedUnicode is an alias for str, so it has no
       effect. */
    Py_INCREF((PyObject*)&PyUnicode_Type);
    PyDict_SetItemString(dict, "OptimizedUnicode", (PyObject*)&PyUnicode_Type);

    /* Set integer constants */
    for (i = 0; _int_constants[i].constant_name != NULL; i++) {
        tmp_obj = PyLong_FromLong(_int_constants[i].constant_value);
        if (!tmp_obj) {
            goto error;
        }
        PyDict_SetItemString(dict, _int_constants[i].constant_name, tmp_obj);
        Py_DECREF(tmp_obj);
    }

    if (!(tmp_obj = PyUnicode_FromString(PYSQLITE_VERSION))) {
        goto error;
    }
    PyDict_SetItemString(dict, "version", tmp_obj);
    Py_DECREF(tmp_obj);

    if (!(tmp_obj = PyUnicode_FromString(sqlite3_libversion()))) {
        goto error;
    }
    PyDict_SetItemString(dict, "sqlite_version", tmp_obj);
    Py_DECREF(tmp_obj);

    /* initialize microprotocols layer */
    pysqlite_microprotocols_init(dict);

    /* initialize the default converters */
    converters_init(dict);

error:
    if (PyErr_Occurred())
    {
        PyErr_SetString(PyExc_ImportError, MODULE_NAME ": init failed");
        Py_DECREF(module);
        module = NULL;
    }
    return module;
}
