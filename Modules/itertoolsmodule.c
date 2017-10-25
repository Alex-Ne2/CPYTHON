
#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "structmember.h"

/* Itertools module written and maintained
   by Raymond D. Hettinger <python@rcn.com>
*/

/*[clinic input]
module itertools
class itertools.groupby "groupbyobject *" "&groupby_type"
class itertools._grouper "_grouperobject *" "&_grouper_type"
class itertools._tee "teeobject *" "&tee_type"
class itertools.teedataobject "teedataobject *" "&teedataobject_type"
class itertools.cycle "cycleobject *" "&cycle_type"
class itertools.dropwhile "dropwhileobject *" "&dropwhile_type"
class itertools.takewhile "takewhileobject *" "&takewhile_type"
class itertools.islice "isliceobject *" "&islice_type"
class itertools.starmap "starmapobject *" "&starmap_type"
class itertools.chain "chainobject *" "&chain_type"
class itertools.product "productobject *" "&product_type"
class itertools.combinations "combinationsobject *" "&combinations_type"
class itertools.combinations_with_replacement "cwrobject *" "&cwr_type"
class itertools.permutations "permutationsobject *" "&permutations_type"
class itertools.accumulate "accumulateobject *" "&accumulate_type"
class itertools.compress "compressobject *" "&compress_type"
class itertools.filterfalse "filterfalseobject *" "&filterfalse_type"
class itertools.count "countobject *" "&count_type"
class itertools.repeat "repeatobject *" "&repeat_type"
class itertools.zip_longest "ziplongestobject *" "&ziplongest_type"
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=95c340f2eacd0350]*/


/* groupby object ************************************************************/

typedef struct {
    PyObject_HEAD
    PyObject *it;
    PyObject *keyfunc;
    PyObject *tgtkey;
    PyObject *currkey;
    PyObject *currvalue;
    const void *currgrouper;  /* borrowed reference */
} groupbyobject;

static PyTypeObject groupby_type;
static PyObject *_grouper_create(groupbyobject *, PyObject *);

/*[clinic input]
@classmethod
itertools.groupby.__new__ as groupby_new

    iterable: object
        Elements to divide into groups according to the key function.
    key: object = None
        Function computing a key value for each element.
        If None, the elements themselves are used for grouping.

Create a groupby object.

This makes an iterator of (key, sub-iterator) pairs. In each such pair, the
sub-iterator is a group of consecutive elements from the input iterable which
all have the same key. The common key for the group is the first item in the
pair.

Example:
>>> # group numbers by their absolute value
>>> elements = [1, -1, 2, 1]
>>> for (key, group) in itertools.groupby(elements, key=abs):
...     print('key={} group={}'.format(key, list(group)))
...
key=1 group=[1, -1]
key=2 group=[2]
key=1 group=[1]
[clinic start generated code]*/

static PyObject *
groupby_new_impl(PyTypeObject *type, PyObject *iterable, PyObject *key)
/*[clinic end generated code: output=7d57fc22bba4949d input=6ad51fd161164a10]*/
{
    groupbyobject *gbo;

    gbo = (groupbyobject *)type->tp_alloc(type, 0);
    if (gbo == NULL)
        return NULL;
    gbo->tgtkey = NULL;
    gbo->currkey = NULL;
    gbo->currvalue = NULL;
    gbo->keyfunc = key;
    Py_INCREF(key);
    gbo->it = PyObject_GetIter(iterable);
    if (gbo->it == NULL) {
        Py_DECREF(gbo);
        return NULL;
    }
    return (PyObject *)gbo;
}

static void
groupby_dealloc(groupbyobject *gbo)
{
    PyObject_GC_UnTrack(gbo);
    Py_XDECREF(gbo->it);
    Py_XDECREF(gbo->keyfunc);
    Py_XDECREF(gbo->tgtkey);
    Py_XDECREF(gbo->currkey);
    Py_XDECREF(gbo->currvalue);
    Py_TYPE(gbo)->tp_free(gbo);
}

static int
groupby_traverse(groupbyobject *gbo, visitproc visit, void *arg)
{
    Py_VISIT(gbo->it);
    Py_VISIT(gbo->keyfunc);
    Py_VISIT(gbo->tgtkey);
    Py_VISIT(gbo->currkey);
    Py_VISIT(gbo->currvalue);
    return 0;
}

Py_LOCAL_INLINE(int)
groupby_step(groupbyobject *gbo)
{
    PyObject *newvalue, *newkey, *oldvalue;

    newvalue = PyIter_Next(gbo->it);
    if (newvalue == NULL)
        return -1;

    if (gbo->keyfunc == Py_None) {
        newkey = newvalue;
        Py_INCREF(newvalue);
    } else {
        newkey = PyObject_CallFunctionObjArgs(gbo->keyfunc, newvalue, NULL);
        if (newkey == NULL) {
            Py_DECREF(newvalue);
            return -1;
        }
    }

    oldvalue = gbo->currvalue;
    gbo->currvalue = newvalue;
    Py_XSETREF(gbo->currkey, newkey);
    Py_XDECREF(oldvalue);
    return 0;
}

static PyObject *
groupby_next(groupbyobject *gbo)
{
    PyObject *r, *grouper;

    gbo->currgrouper = NULL;
    /* skip to next iteration group */
    for (;;) {
        if (gbo->currkey == NULL)
            /* pass */;
        else if (gbo->tgtkey == NULL)
            break;
        else {
            int rcmp;

            rcmp = PyObject_RichCompareBool(gbo->tgtkey, gbo->currkey, Py_EQ);
            if (rcmp == -1)
                return NULL;
            else if (rcmp == 0)
                break;
        }

        if (groupby_step(gbo) < 0)
            return NULL;
    }
    Py_INCREF(gbo->currkey);
    Py_XSETREF(gbo->tgtkey, gbo->currkey);

    grouper = _grouper_create(gbo, gbo->tgtkey);
    if (grouper == NULL)
        return NULL;

    r = PyTuple_Pack(2, gbo->currkey, grouper);
    Py_DECREF(grouper);
    return r;
}

/*[clinic input]
itertools.groupby.__reduce__ as groupby_reduce

    lz: self(type="groupbyobject *")

Return state information for pickling.
[clinic start generated code]*/

static PyObject *
groupby_reduce_impl(groupbyobject *lz)
/*[clinic end generated code: output=81ccca5e1cd6ef82 input=c9328f12aa213161]*/
{
    /* reduce as a 'new' call with an optional 'setstate' if groupby
     * has started
     */
    PyObject *value;
    if (lz->tgtkey && lz->currkey && lz->currvalue)
        value = Py_BuildValue("O(OO)(OOO)", Py_TYPE(lz),
            lz->it, lz->keyfunc, lz->currkey, lz->currvalue, lz->tgtkey);
    else
        value = Py_BuildValue("O(OO)", Py_TYPE(lz),
            lz->it, lz->keyfunc);

    return value;
}

/*[clinic input]
itertools.groupby.__setstate__ as groupby_setstate

    lz: self(type="groupbyobject *")
    state: object
    /

Set state information for unpickling.
[clinic start generated code]*/

static PyObject *
groupby_setstate(groupbyobject *lz, PyObject *state)
/*[clinic end generated code: output=d668c44985ed8d2c input=1c009772af2ae55f]*/
{
    PyObject *currkey, *currvalue, *tgtkey;
    if (!PyTuple_Check(state)) {
        PyErr_SetString(PyExc_TypeError, "state is not a tuple");
        return NULL;
    }
    if (!PyArg_ParseTuple(state, "OOO", &currkey, &currvalue, &tgtkey)) {
        return NULL;
    }
    Py_INCREF(currkey);
    Py_XSETREF(lz->currkey, currkey);
    Py_INCREF(currvalue);
    Py_XSETREF(lz->currvalue, currvalue);
    Py_INCREF(tgtkey);
    Py_XSETREF(lz->tgtkey, tgtkey);
    Py_RETURN_NONE;
}

static PyMethodDef groupby_methods[] = {
    GROUPBY_REDUCE_METHODDEF
    GROUPBY_SETSTATE_METHODDEF
    {NULL,              NULL}           /* sentinel */
};

static PyTypeObject groupby_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.groupby",                /* tp_name */
    sizeof(groupbyobject),              /* tp_basicsize */
    0,                                  /* tp_itemsize */
    /* methods */
    (destructor)groupby_dealloc,        /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_reserved */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    0,                                  /* tp_call */
    0,                                  /* tp_str */
    PyObject_GenericGetAttr,            /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,            /* tp_flags */
    groupby_new__doc__,                 /* tp_doc */
    (traverseproc)groupby_traverse,     /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    PyObject_SelfIter,                  /* tp_iter */
    (iternextfunc)groupby_next,         /* tp_iternext */
    groupby_methods,                    /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    groupby_new,                        /* tp_new */
    PyObject_GC_Del,                    /* tp_free */
};


/* _grouper object (internal) ************************************************/

typedef struct {
    PyObject_HEAD
    PyObject *parent;
    PyObject *tgtkey;
} _grouperobject;

static PyTypeObject _grouper_type;

/*[clinic input]
@classmethod
itertools._grouper.__new__ as _grouper_new

    parent: object(subclass_of='&groupby_type')
    tgtkey: object
    /
[clinic start generated code]*/

static PyObject *
_grouper_new_impl(PyTypeObject *type, PyObject *parent, PyObject *tgtkey)
/*[clinic end generated code: output=c2b3fdd3561b1103 input=280c2659160f7890]*/
{
    return _grouper_create((groupbyobject*) parent, tgtkey);
}

static PyObject *
_grouper_create(groupbyobject *parent, PyObject *tgtkey)
{
    _grouperobject *igo;

    igo = PyObject_GC_New(_grouperobject, &_grouper_type);
    if (igo == NULL)
        return NULL;
    igo->parent = (PyObject *)parent;
    Py_INCREF(parent);
    igo->tgtkey = tgtkey;
    Py_INCREF(tgtkey);
    parent->currgrouper = igo;  /* borrowed reference */

    PyObject_GC_Track(igo);
    return (PyObject *)igo;
}

static void
_grouper_dealloc(_grouperobject *igo)
{
    PyObject_GC_UnTrack(igo);
    Py_DECREF(igo->parent);
    Py_DECREF(igo->tgtkey);
    PyObject_GC_Del(igo);
}

static int
_grouper_traverse(_grouperobject *igo, visitproc visit, void *arg)
{
    Py_VISIT(igo->parent);
    Py_VISIT(igo->tgtkey);
    return 0;
}

static PyObject *
_grouper_next(_grouperobject *igo)
{
    groupbyobject *gbo = (groupbyobject *)igo->parent;
    PyObject *r;
    int rcmp;

    if (gbo->currgrouper != igo)
        return NULL;
    if (gbo->currvalue == NULL) {
        if (groupby_step(gbo) < 0)
            return NULL;
    }

    assert(gbo->currkey != NULL);
    rcmp = PyObject_RichCompareBool(igo->tgtkey, gbo->currkey, Py_EQ);
    if (rcmp <= 0)
        /* got any error or current group is end */
        return NULL;

    r = gbo->currvalue;
    gbo->currvalue = NULL;
    Py_CLEAR(gbo->currkey);

    return r;
}

/*[clinic input]
itertools._grouper.__reduce__ as _grouper_reduce

    lz: self(type="_grouperobject *")

Return state information for pickling.
[clinic start generated code]*/

static PyObject *
_grouper_reduce_impl(_grouperobject *lz)
/*[clinic end generated code: output=5d852dbd29c55aaf input=89204ccad93c8271]*/
{
    if (((groupbyobject *)lz->parent)->currgrouper != lz) {
        return Py_BuildValue("N(())", _PyObject_GetBuiltin("iter"));
    }
    return Py_BuildValue("O(OO)", Py_TYPE(lz), lz->parent, lz->tgtkey);
}

static PyMethodDef _grouper_methods[] = {
    _GROUPER_REDUCE_METHODDEF
    {NULL,              NULL}   /* sentinel */
};


static PyTypeObject _grouper_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools._grouper",               /* tp_name */
    sizeof(_grouperobject),             /* tp_basicsize */
    0,                                  /* tp_itemsize */
    /* methods */
    (destructor)_grouper_dealloc,       /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_reserved */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    0,                                  /* tp_call */
    0,                                  /* tp_str */
    PyObject_GenericGetAttr,            /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,            /* tp_flags */
    0,                                  /* tp_doc */
    (traverseproc)_grouper_traverse,    /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    PyObject_SelfIter,                  /* tp_iter */
    (iternextfunc)_grouper_next,        /* tp_iternext */
    _grouper_methods,                   /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    _grouper_new,                       /* tp_new */
    PyObject_GC_Del,                    /* tp_free */
};


/* tee object and with supporting function and objects ***********************/

/* The teedataobject pre-allocates space for LINKCELLS number of objects.
   To help the object fit neatly inside cache lines (space for 16 to 32
   pointers), the value should be a multiple of 16 minus  space for
   the other structure members including PyHEAD overhead.  The larger the
   value, the less memory overhead per object and the less time spent
   allocating/deallocating new links.  The smaller the number, the less
   wasted space and the more rapid freeing of older data.
*/
#define LINKCELLS 57

typedef struct {
    PyObject_HEAD
    PyObject *it;
    int numread;                /* 0 <= numread <= LINKCELLS */
    PyObject *nextlink;
    PyObject *(values[LINKCELLS]);
} teedataobject;

typedef struct {
    PyObject_HEAD
    teedataobject *dataobj;
    int index;                  /* 0 <= index <= LINKCELLS */
    PyObject *weakreflist;
} teeobject;

static PyTypeObject teedataobject_type;

static PyObject *
teedataobject_newinternal(PyObject *it)
{
    teedataobject *tdo;

    tdo = PyObject_GC_New(teedataobject, &teedataobject_type);
    if (tdo == NULL)
        return NULL;

    tdo->numread = 0;
    tdo->nextlink = NULL;
    Py_INCREF(it);
    tdo->it = it;
    PyObject_GC_Track(tdo);
    return (PyObject *)tdo;
}

static PyObject *
teedataobject_jumplink(teedataobject *tdo)
{
    if (tdo->nextlink == NULL)
        tdo->nextlink = teedataobject_newinternal(tdo->it);
    Py_XINCREF(tdo->nextlink);
    return tdo->nextlink;
}

static PyObject *
teedataobject_getitem(teedataobject *tdo, int i)
{
    PyObject *value;

    assert(i < LINKCELLS);
    if (i < tdo->numread)
        value = tdo->values[i];
    else {
        /* this is the lead iterator, so fetch more data */
        assert(i == tdo->numread);
        value = PyIter_Next(tdo->it);
        if (value == NULL)
            return NULL;
        tdo->numread++;
        tdo->values[i] = value;
    }
    Py_INCREF(value);
    return value;
}

static int
teedataobject_traverse(teedataobject *tdo, visitproc visit, void * arg)
{
    int i;

    Py_VISIT(tdo->it);
    for (i = 0; i < tdo->numread; i++)
        Py_VISIT(tdo->values[i]);
    Py_VISIT(tdo->nextlink);
    return 0;
}

static void
teedataobject_safe_decref(PyObject *obj)
{
    while (obj && Py_TYPE(obj) == &teedataobject_type &&
           Py_REFCNT(obj) == 1) {
        PyObject *nextlink = ((teedataobject *)obj)->nextlink;
        ((teedataobject *)obj)->nextlink = NULL;
        Py_DECREF(obj);
        obj = nextlink;
    }
    Py_XDECREF(obj);
}

static int
teedataobject_clear(teedataobject *tdo)
{
    int i;
    PyObject *tmp;

    Py_CLEAR(tdo->it);
    for (i=0 ; i<tdo->numread ; i++)
        Py_CLEAR(tdo->values[i]);
    tmp = tdo->nextlink;
    tdo->nextlink = NULL;
    teedataobject_safe_decref(tmp);
    return 0;
}

static void
teedataobject_dealloc(teedataobject *tdo)
{
    PyObject_GC_UnTrack(tdo);
    teedataobject_clear(tdo);
    PyObject_GC_Del(tdo);
}

/*[clinic input]
itertools.teedataobject.__reduce__ as teedataobject_reduce

    tdo: self(type="teedataobject *")

Return state information for pickling.
[clinic start generated code]*/

static PyObject *
teedataobject_reduce_impl(teedataobject *tdo)
/*[clinic end generated code: output=2dd0ee192b561019 input=5067dad6127ce165]*/
{
    int i;
    /* create a temporary list of already iterated values */
    PyObject *values = PyList_New(tdo->numread);

    if (!values)
        return NULL;
    for (i=0 ; i<tdo->numread ; i++) {
        Py_INCREF(tdo->values[i]);
        PyList_SET_ITEM(values, i, tdo->values[i]);
    }
    return Py_BuildValue("O(ONO)", Py_TYPE(tdo), tdo->it,
                         values,
                         tdo->nextlink ? tdo->nextlink : Py_None);
}

static PyTypeObject teedataobject_type;

/*[clinic input]
@classmethod
itertools.teedataobject.__new__ as teedataobject_new

    iterable: object
    values: object(subclass_of='&PyList_Type')
    next: object
    /

Data container common to multiple tee objects.
[clinic start generated code]*/

static PyObject *
teedataobject_new_impl(PyTypeObject *type, PyObject *iterable,
                       PyObject *values, PyObject *next)
/*[clinic end generated code: output=9bc76065decb56f6 input=cb41f9058761382a]*/
{
    teedataobject *tdo;
    Py_ssize_t i, len;

    assert(type == &teedataobject_type);

    tdo = (teedataobject *)teedataobject_newinternal(iterable);
    if (!tdo)
        return NULL;

    len = PyList_GET_SIZE(values);
    if (len > LINKCELLS)
        goto err;
    for (i=0; i<len; i++) {
        tdo->values[i] = PyList_GET_ITEM(values, i);
        Py_INCREF(tdo->values[i]);
    }
    /* len <= LINKCELLS < INT_MAX */
    tdo->numread = Py_SAFE_DOWNCAST(len, Py_ssize_t, int);

    if (len == LINKCELLS) {
        if (next != Py_None) {
            if (Py_TYPE(next) != &teedataobject_type)
                goto err;
            assert(tdo->nextlink == NULL);
            Py_INCREF(next);
            tdo->nextlink = next;
        }
    } else {
        if (next != Py_None)
            goto err; /* shouldn't have a next if we are not full */
    }
    return (PyObject*)tdo;

err:
    Py_XDECREF(tdo);
    PyErr_SetString(PyExc_ValueError, "Invalid arguments");
    return NULL;
}

static PyMethodDef teedataobject_methods[] = {
    TEEDATAOBJECT_REDUCE_METHODDEF
    {NULL,              NULL}           /* sentinel */
};

static PyTypeObject teedataobject_type = {
    PyVarObject_HEAD_INIT(0, 0)                 /* Must fill in type value later */
    "itertools._tee_dataobject",                /* tp_name */
    sizeof(teedataobject),                      /* tp_basicsize */
    0,                                          /* tp_itemsize */
    /* methods */
    (destructor)teedataobject_dealloc,          /* tp_dealloc */
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
    PyObject_GenericGetAttr,                    /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,    /* tp_flags */
    teedataobject_new__doc__,                   /* tp_doc */
    (traverseproc)teedataobject_traverse,       /* tp_traverse */
    (inquiry)teedataobject_clear,               /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    0,                                          /* tp_iter */
    0,                                          /* tp_iternext */
    teedataobject_methods,                      /* tp_methods */
    0,                                          /* tp_members */
    0,                                          /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    0,                                          /* tp_init */
    0,                                          /* tp_alloc */
    teedataobject_new,                          /* tp_new */
    PyObject_GC_Del,                            /* tp_free */
};


static PyTypeObject tee_type;

static PyObject *
tee_next(teeobject *to)
{
    PyObject *value, *link;

    if (to->index >= LINKCELLS) {
        link = teedataobject_jumplink(to->dataobj);
        if (link == NULL)
            return NULL;
        Py_SETREF(to->dataobj, (teedataobject *)link);
        to->index = 0;
    }
    value = teedataobject_getitem(to->dataobj, to->index);
    if (value == NULL)
        return NULL;
    to->index++;
    return value;
}

static int
tee_traverse(teeobject *to, visitproc visit, void *arg)
{
    Py_VISIT((PyObject *)to->dataobj);
    return 0;
}

/*[clinic input]
itertools._tee.__copy__ as tee_copy

    to: self(type="teeobject *")

Returns an independent iterator.
[clinic start generated code]*/

static PyObject *
tee_copy_impl(teeobject *to)
/*[clinic end generated code: output=45629a9ebc19de67 input=da64ef9fac60a134]*/
{
    teeobject *newto;

    newto = PyObject_GC_New(teeobject, &tee_type);
    if (newto == NULL)
        return NULL;
    Py_INCREF(to->dataobj);
    newto->dataobj = to->dataobj;
    newto->index = to->index;
    newto->weakreflist = NULL;
    PyObject_GC_Track(newto);
    return (PyObject *)newto;
}

static PyObject *
tee_fromiterable(PyObject *iterable)
{
    teeobject *to;
    PyObject *it = NULL;

    it = PyObject_GetIter(iterable);
    if (it == NULL)
        return NULL;
    if (PyObject_TypeCheck(it, &tee_type)) {
        to = (teeobject *)tee_copy_impl((teeobject *)it);
        goto done;
    }

    to = PyObject_GC_New(teeobject, &tee_type);
    if (to == NULL)
        goto done;
    to->dataobj = (teedataobject *)teedataobject_newinternal(it);
    if (!to->dataobj) {
        PyObject_GC_Del(to);
        to = NULL;
        goto done;
    }

    to->index = 0;
    to->weakreflist = NULL;
    PyObject_GC_Track(to);
done:
    Py_XDECREF(it);
    return (PyObject *)to;
}

/*[clinic input]
@classmethod
itertools._tee.__new__ as tee_new

    iterable: object
    /

Create a _tee object.

An iterator wrapped to make it copyable.
[clinic start generated code]*/

static PyObject *
tee_new_impl(PyTypeObject *type, PyObject *iterable)
/*[clinic end generated code: output=35aa9c64122b5871 input=827b5f7b19c54dfa]*/
{
    return tee_fromiterable(iterable);
}

static int
tee_clear(teeobject *to)
{
    if (to->weakreflist != NULL)
        PyObject_ClearWeakRefs((PyObject *) to);
    Py_CLEAR(to->dataobj);
    return 0;
}

static void
tee_dealloc(teeobject *to)
{
    PyObject_GC_UnTrack(to);
    tee_clear(to);
    PyObject_GC_Del(to);
}

/*[clinic input]
itertools._tee.__reduce__ as tee_reduce

    to: self(type="teeobject *")

Return state information for pickling.
[clinic start generated code]*/

static PyObject *
tee_reduce_impl(teeobject *to)
/*[clinic end generated code: output=46a6d7b07fec2a40 input=63c43d1a548a83c8]*/
{
    return Py_BuildValue("O(())(Oi)", Py_TYPE(to), to->dataobj, to->index);
}

/*[clinic input]
itertools._tee.__setstate__ as tee_setstate

    to: self(type="teeobject *")
    state: object
    /

Set state information for unpickling.
[clinic start generated code]*/

static PyObject *
tee_setstate(teeobject *to, PyObject *state)
/*[clinic end generated code: output=3ff5ca72492e2610 input=1ed8907a9f02d536]*/
{
    teedataobject *tdo;
    int index;
    if (!PyTuple_Check(state)) {
        PyErr_SetString(PyExc_TypeError, "state is not a tuple");
        return NULL;
    }
    if (!PyArg_ParseTuple(state, "O!i", &teedataobject_type, &tdo, &index)) {
        return NULL;
    }
    if (index < 0 || index > LINKCELLS) {
        PyErr_SetString(PyExc_ValueError, "Index out of range");
        return NULL;
    }
    Py_INCREF(tdo);
    Py_XSETREF(to->dataobj, tdo);
    to->index = index;
    Py_RETURN_NONE;
}

static PyMethodDef tee_methods[] = {
    TEE_COPY_METHODDEF
    TEE_REDUCE_METHODDEF
    TEE_SETSTATE_METHODDEF
    {NULL,              NULL}           /* sentinel */
};

static PyTypeObject tee_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools._tee",                   /* tp_name */
    sizeof(teeobject),                  /* tp_basicsize */
    0,                                  /* tp_itemsize */
    /* methods */
    (destructor)tee_dealloc,            /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_reserved */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    0,                                  /* tp_call */
    0,                                  /* tp_str */
    0,                                  /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,            /* tp_flags */
    tee_new__doc__,                     /* tp_doc */
    (traverseproc)tee_traverse,         /* tp_traverse */
    (inquiry)tee_clear,                 /* tp_clear */
    0,                                  /* tp_richcompare */
    offsetof(teeobject, weakreflist),   /* tp_weaklistoffset */
    PyObject_SelfIter,                  /* tp_iter */
    (iternextfunc)tee_next,             /* tp_iternext */
    tee_methods,                        /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    tee_new,                            /* tp_new */
    PyObject_GC_Del,                    /* tp_free */
};

/*[clinic input]
itertools.tee as tee

    iterable: object
    n: Py_ssize_t = 2
        number of independent iterators to return
    /

Returns a tuple of n independent iterators from a single iterable.

Once this has been called, the original iterable should not be used anywhere
else; otherwise, the iterable could get advanced without the tee objects (those
in the returned tuple) being informed.
[clinic start generated code]*/

static PyObject *
tee_impl(PyObject *module, PyObject *iterable, Py_ssize_t n)
/*[clinic end generated code: output=5159f48b66c422b4 input=a72b8ce4e9aa7cc9]*/
{
    Py_ssize_t i;
    PyObject *it, *copyable, *result;
    _Py_IDENTIFIER(__copy__);

    if (n < 0) {
        PyErr_SetString(PyExc_ValueError, "n must be >= 0");
        return NULL;
    }
    result = PyTuple_New(n);
    if (result == NULL)
        return NULL;
    if (n == 0)
        return result;
    it = PyObject_GetIter(iterable);
    if (it == NULL) {
        Py_DECREF(result);
        return NULL;
    }
    if (!_PyObject_HasAttrId(it, &PyId___copy__)) {
        copyable = tee_fromiterable(it);
        Py_DECREF(it);
        if (copyable == NULL) {
            Py_DECREF(result);
            return NULL;
        }
    } else
        copyable = it;
    PyTuple_SET_ITEM(result, 0, copyable);
    for (i=1 ; i<n ; i++) {

        copyable = _PyObject_CallMethodId(copyable, &PyId___copy__, NULL);
        if (copyable == NULL) {
            Py_DECREF(result);
            return NULL;
        }
        PyTuple_SET_ITEM(result, i, copyable);
    }
    return result;
}


/* cycle object **************************************************************/

typedef struct {
    PyObject_HEAD
    PyObject *it;
    PyObject *saved;
    Py_ssize_t index;
    int firstpass;
} cycleobject;

static PyTypeObject cycle_type;

/*[clinic input]
@classmethod
itertools.cycle.__new__ as cycle_new

    iterable: object
    /

Create a cycle object.

This object will return elements from the iterable until it is exhausted.
Then it will repeat the sequence indefinitely.
[clinic start generated code]*/

static PyObject *
cycle_new_impl(PyTypeObject *type, PyObject *iterable)
/*[clinic end generated code: output=9c3c6a645f54603e input=fdd6c11dca6b1efe]*/
{
    PyObject *it;
    PyObject *saved;
    cycleobject *lz;

    /* Get iterator. */
    it = PyObject_GetIter(iterable);
    if (it == NULL)
        return NULL;

    saved = PyList_New(0);
    if (saved == NULL) {
        Py_DECREF(it);
        return NULL;
    }

    /* create cycleobject structure */
    lz = (cycleobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_DECREF(it);
        Py_DECREF(saved);
        return NULL;
    }
    lz->it = it;
    lz->saved = saved;
    lz->index = 0;
    lz->firstpass = 0;

    return (PyObject *)lz;
}

static void
cycle_dealloc(cycleobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->it);
    Py_XDECREF(lz->saved);
    Py_TYPE(lz)->tp_free(lz);
}

static int
cycle_traverse(cycleobject *lz, visitproc visit, void *arg)
{
    if (lz->it)
        Py_VISIT(lz->it);
    Py_VISIT(lz->saved);
    return 0;
}

static PyObject *
cycle_next(cycleobject *lz)
{
    PyObject *item;

    if (lz->it != NULL) {
        item = PyIter_Next(lz->it);
        if (item != NULL) {
            if (lz->firstpass)
                return item;
            if (PyList_Append(lz->saved, item)) {
                Py_DECREF(item);
                return NULL;
            }
            return item;
        }
        /* Note:  StopIteration is already cleared by PyIter_Next() */
        if (PyErr_Occurred())
            return NULL;
        Py_CLEAR(lz->it);
    }
    if (PyList_GET_SIZE(lz->saved) == 0)
        return NULL;
    item = PyList_GET_ITEM(lz->saved, lz->index);
    lz->index++;
    if (lz->index >= PyList_GET_SIZE(lz->saved))
        lz->index = 0;
    Py_INCREF(item);
    return item;
}

/*[clinic input]
itertools.cycle.__reduce__ as cycle_reduce

    lz: self(type="cycleobject *")

Return state information for pickling.
[clinic start generated code]*/

static PyObject *
cycle_reduce_impl(cycleobject *lz)
/*[clinic end generated code: output=f2b1f8bfd1d6423a input=248c55bc398cd5fa]*/
{
    /* Create a new cycle with the iterator tuple, then set the saved state */
    if (lz->it == NULL) {
        PyObject *it = PyObject_GetIter(lz->saved);
        if (it == NULL)
            return NULL;
        if (lz->index != 0) {
            _Py_IDENTIFIER(__setstate__);
            PyObject *res = _PyObject_CallMethodId(it, &PyId___setstate__,
                                                   "n", lz->index);
            if (res == NULL) {
                Py_DECREF(it);
                return NULL;
            }
            Py_DECREF(res);
        }
        return Py_BuildValue("O(N)(Oi)", Py_TYPE(lz), it, lz->saved, 1);
    }
    return Py_BuildValue("O(O)(Oi)", Py_TYPE(lz), lz->it, lz->saved,
                         lz->firstpass);
}

/*[clinic input]
itertools.cycle.__setstate__ as cycle_setstate

    lz: self(type="cycleobject *")
    state: object
    /

Set state information for unpickling.
[clinic start generated code]*/

static PyObject *
cycle_setstate(cycleobject *lz, PyObject *state)
/*[clinic end generated code: output=9c82d80fdebf7705 input=840f2f296fe555a9]*/
{
    PyObject *saved=NULL;
    int firstpass;
    if (!PyTuple_Check(state)) {
        PyErr_SetString(PyExc_TypeError, "state is not a tuple");
        return NULL;
    }
    if (!PyArg_ParseTuple(state, "O!i", &PyList_Type, &saved, &firstpass)) {
        return NULL;
    }
    Py_INCREF(saved);
    Py_XSETREF(lz->saved, saved);
    lz->firstpass = firstpass != 0;
    lz->index = 0;
    Py_RETURN_NONE;
}

static PyMethodDef cycle_methods[] = {
    CYCLE_REDUCE_METHODDEF
    CYCLE_SETSTATE_METHODDEF
    {NULL,              NULL}   /* sentinel */
};


static PyTypeObject cycle_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.cycle",                  /* tp_name */
    sizeof(cycleobject),                /* tp_basicsize */
    0,                                  /* tp_itemsize */
    /* methods */
    (destructor)cycle_dealloc,          /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_reserved */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    0,                                  /* tp_call */
    0,                                  /* tp_str */
    PyObject_GenericGetAttr,            /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,            /* tp_flags */
    cycle_new__doc__,                   /* tp_doc */
    (traverseproc)cycle_traverse,       /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    PyObject_SelfIter,                  /* tp_iter */
    (iternextfunc)cycle_next,           /* tp_iternext */
    cycle_methods,                      /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    cycle_new,                          /* tp_new */
    PyObject_GC_Del,                    /* tp_free */
};


/* dropwhile object **********************************************************/

typedef struct {
    PyObject_HEAD
    PyObject *func;
    PyObject *it;
    long start;
} dropwhileobject;

static PyTypeObject dropwhile_type;

/*[clinic input]
@classmethod
itertools.dropwhile.__new__ as dropwhile_new

    predicate: object
    iterable: object
    /

Create a dropwhile object.

Drops items from the iterable while predicate(item) is true.
Afterwards, returns every element until the iterable is exhausted.
[clinic start generated code]*/

static PyObject *
dropwhile_new_impl(PyTypeObject *type, PyObject *predicate,
                   PyObject *iterable)
/*[clinic end generated code: output=68ccecb645b70e7c input=b4d0e3c961817403]*/
{
    PyObject *it;
    dropwhileobject *lz;

    /* Get iterator. */
    it = PyObject_GetIter(iterable);
    if (it == NULL)
        return NULL;

    /* create dropwhileobject structure */
    lz = (dropwhileobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_DECREF(it);
        return NULL;
    }
    Py_INCREF(predicate);
    lz->func = predicate;
    lz->it = it;
    lz->start = 0;

    return (PyObject *)lz;
}

static void
dropwhile_dealloc(dropwhileobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->func);
    Py_XDECREF(lz->it);
    Py_TYPE(lz)->tp_free(lz);
}

static int
dropwhile_traverse(dropwhileobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->it);
    Py_VISIT(lz->func);
    return 0;
}

static PyObject *
dropwhile_next(dropwhileobject *lz)
{
    PyObject *item, *good;
    PyObject *it = lz->it;
    long ok;
    PyObject *(*iternext)(PyObject *);

    iternext = *Py_TYPE(it)->tp_iternext;
    for (;;) {
        item = iternext(it);
        if (item == NULL)
            return NULL;
        if (lz->start == 1)
            return item;

        good = PyObject_CallFunctionObjArgs(lz->func, item, NULL);
        if (good == NULL) {
            Py_DECREF(item);
            return NULL;
        }
        ok = PyObject_IsTrue(good);
        Py_DECREF(good);
        if (ok == 0) {
            lz->start = 1;
            return item;
        }
        Py_DECREF(item);
        if (ok < 0)
            return NULL;
    }
}

/*[clinic input]
itertools.dropwhile.__reduce__ as dropwhile_reduce

    lz: self(type="dropwhileobject *")

Return state information for pickling.
[clinic start generated code]*/

static PyObject *
dropwhile_reduce_impl(dropwhileobject *lz)
/*[clinic end generated code: output=df490b72b8892732 input=6abc87d157f1ff18]*/
{
    return Py_BuildValue("O(OO)l", Py_TYPE(lz), lz->func, lz->it, lz->start);
}

/*[clinic input]
itertools.dropwhile.__setstate__ as dropwhile_setstate

    lz: self(type="dropwhileobject *")
    state: object
    /

Set state information for unpickling.
[clinic start generated code]*/

static PyObject *
dropwhile_setstate(dropwhileobject *lz, PyObject *state)
/*[clinic end generated code: output=fd28c2f3fc19f7a1 input=397b3ecd83cbe6a4]*/
{
    int start = PyObject_IsTrue(state);
    if (start < 0)
        return NULL;
    lz->start = start;
    Py_RETURN_NONE;
}

static PyMethodDef dropwhile_methods[] = {
    DROPWHILE_REDUCE_METHODDEF
    DROPWHILE_SETSTATE_METHODDEF
    {NULL,              NULL}   /* sentinel */
};

static PyTypeObject dropwhile_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.dropwhile",              /* tp_name */
    sizeof(dropwhileobject),            /* tp_basicsize */
    0,                                  /* tp_itemsize */
    /* methods */
    (destructor)dropwhile_dealloc,      /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_reserved */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    0,                                  /* tp_call */
    0,                                  /* tp_str */
    PyObject_GenericGetAttr,            /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,            /* tp_flags */
    dropwhile_new__doc__,               /* tp_doc */
    (traverseproc)dropwhile_traverse,   /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    PyObject_SelfIter,                  /* tp_iter */
    (iternextfunc)dropwhile_next,       /* tp_iternext */
    dropwhile_methods,                  /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    dropwhile_new,                      /* tp_new */
    PyObject_GC_Del,                    /* tp_free */
};


/* takewhile object **********************************************************/

typedef struct {
    PyObject_HEAD
    PyObject *func;
    PyObject *it;
    long stop;
} takewhileobject;

static PyTypeObject takewhile_type;

/*[clinic input]
@classmethod
itertools.takewhile.__new__ as takewhile_new

    predicate: object
    iterable: object
    /

Create a takewhile object.

Return successive entries from an iterable as long as the
predicate evaluates to true for each entry.
[clinic start generated code]*/

static PyObject *
takewhile_new_impl(PyTypeObject *type, PyObject *predicate,
                   PyObject *iterable)
/*[clinic end generated code: output=ec1277335df81046 input=984bd1d6e5854510]*/
{
    PyObject *it;
    takewhileobject *lz;

    /* Get iterator. */
    it = PyObject_GetIter(iterable);
    if (it == NULL)
        return NULL;

    /* create takewhileobject structure */
    lz = (takewhileobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_DECREF(it);
        return NULL;
    }
    Py_INCREF(predicate);
    lz->func = predicate;
    lz->it = it;
    lz->stop = 0;

    return (PyObject *)lz;
}

static void
takewhile_dealloc(takewhileobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->func);
    Py_XDECREF(lz->it);
    Py_TYPE(lz)->tp_free(lz);
}

static int
takewhile_traverse(takewhileobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->it);
    Py_VISIT(lz->func);
    return 0;
}

static PyObject *
takewhile_next(takewhileobject *lz)
{
    PyObject *item, *good;
    PyObject *it = lz->it;
    long ok;

    if (lz->stop == 1)
        return NULL;

    item = (*Py_TYPE(it)->tp_iternext)(it);
    if (item == NULL)
        return NULL;

    good = PyObject_CallFunctionObjArgs(lz->func, item, NULL);
    if (good == NULL) {
        Py_DECREF(item);
        return NULL;
    }
    ok = PyObject_IsTrue(good);
    Py_DECREF(good);
    if (ok > 0)
        return item;
    Py_DECREF(item);
    if (ok == 0)
        lz->stop = 1;
    return NULL;
}

/*[clinic input]
itertools.takewhile.__reduce__ as takewhile_reduce

    lz: self(type="takewhileobject *")

Return state information for pickling.
[clinic start generated code]*/

static PyObject *
takewhile_reduce_impl(takewhileobject *lz)
/*[clinic end generated code: output=f7b9e63cb66dc9e7 input=20da021310e98c13]*/
{
    return Py_BuildValue("O(OO)l", Py_TYPE(lz), lz->func, lz->it, lz->stop);
}

/*[clinic input]
itertools.takewhile.__setstate__ as takewhile_setstate

    lz: self(type="takewhileobject *")
    state: object
    /

Set state information for unpickling.
[clinic start generated code]*/

static PyObject *
takewhile_setstate(takewhileobject *lz, PyObject *state)
/*[clinic end generated code: output=66165bddfa716106 input=84532fc48b901e96]*/
{
    int stop = PyObject_IsTrue(state);

    if (stop < 0)
        return NULL;
    lz->stop = stop;
    Py_RETURN_NONE;
}

static PyMethodDef takewhile_reduce_methods[] = {
    TAKEWHILE_REDUCE_METHODDEF
    TAKEWHILE_SETSTATE_METHODDEF
    {NULL,              NULL}   /* sentinel */
};

static PyTypeObject takewhile_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.takewhile",              /* tp_name */
    sizeof(takewhileobject),            /* tp_basicsize */
    0,                                  /* tp_itemsize */
    /* methods */
    (destructor)takewhile_dealloc,      /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_reserved */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    0,                                  /* tp_call */
    0,                                  /* tp_str */
    PyObject_GenericGetAttr,            /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,            /* tp_flags */
    takewhile_new__doc__,               /* tp_doc */
    (traverseproc)takewhile_traverse,   /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    PyObject_SelfIter,                  /* tp_iter */
    (iternextfunc)takewhile_next,       /* tp_iternext */
    takewhile_reduce_methods,           /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    takewhile_new,                      /* tp_new */
    PyObject_GC_Del,                    /* tp_free */
};


/* islice object *************************************************************/

typedef struct {
    PyObject_HEAD
    PyObject *it;
    Py_ssize_t next;
    Py_ssize_t stop;
    Py_ssize_t step;
    Py_ssize_t cnt;
} isliceobject;

static PyTypeObject islice_type;

static PyObject *
islice_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *seq;
    Py_ssize_t start=0, stop=-1, step=1;
    PyObject *it, *a1=NULL, *a2=NULL, *a3=NULL;
    Py_ssize_t numargs;
    isliceobject *lz;

    if (type == &islice_type && !_PyArg_NoKeywords("islice", kwds))
        return NULL;

    if (!PyArg_UnpackTuple(args, "islice", 2, 4, &seq, &a1, &a2, &a3))
        return NULL;

    numargs = PyTuple_Size(args);
    if (numargs == 2) {
        if (a1 != Py_None) {
            stop = PyNumber_AsSsize_t(a1, PyExc_OverflowError);
            if (stop == -1) {
                if (PyErr_Occurred())
                    PyErr_Clear();
                PyErr_SetString(PyExc_ValueError,
                   "Stop argument for islice() must be None or "
                   "an integer: 0 <= x <= sys.maxsize.");
                return NULL;
            }
        }
    } else {
        if (a1 != Py_None)
            start = PyNumber_AsSsize_t(a1, PyExc_OverflowError);
        if (start == -1 && PyErr_Occurred())
            PyErr_Clear();
        if (a2 != Py_None) {
            stop = PyNumber_AsSsize_t(a2, PyExc_OverflowError);
            if (stop == -1) {
                if (PyErr_Occurred())
                    PyErr_Clear();
                PyErr_SetString(PyExc_ValueError,
                   "Stop argument for islice() must be None or "
                   "an integer: 0 <= x <= sys.maxsize.");
                return NULL;
            }
        }
    }
    if (start<0 || stop<-1) {
        PyErr_SetString(PyExc_ValueError,
           "Indices for islice() must be None or "
           "an integer: 0 <= x <= sys.maxsize.");
        return NULL;
    }

    if (a3 != NULL) {
        if (a3 != Py_None)
            step = PyNumber_AsSsize_t(a3, PyExc_OverflowError);
        if (step == -1 && PyErr_Occurred())
            PyErr_Clear();
    }
    if (step<1) {
        PyErr_SetString(PyExc_ValueError,
           "Step for islice() must be a positive integer or None.");
        return NULL;
    }

    /* Get iterator. */
    it = PyObject_GetIter(seq);
    if (it == NULL)
        return NULL;

    /* create isliceobject structure */
    lz = (isliceobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_DECREF(it);
        return NULL;
    }
    lz->it = it;
    lz->next = start;
    lz->stop = stop;
    lz->step = step;
    lz->cnt = 0L;

    return (PyObject *)lz;
}

static void
islice_dealloc(isliceobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->it);
    Py_TYPE(lz)->tp_free(lz);
}

static int
islice_traverse(isliceobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->it);
    return 0;
}

static PyObject *
islice_next(isliceobject *lz)
{
    PyObject *item;
    PyObject *it = lz->it;
    Py_ssize_t stop = lz->stop;
    Py_ssize_t oldnext;
    PyObject *(*iternext)(PyObject *);

    if (it == NULL)
        return NULL;

    iternext = *Py_TYPE(it)->tp_iternext;
    while (lz->cnt < lz->next) {
        item = iternext(it);
        if (item == NULL)
            goto empty;
        Py_DECREF(item);
        lz->cnt++;
    }
    if (stop != -1 && lz->cnt >= stop)
        goto empty;
    item = iternext(it);
    if (item == NULL)
        goto empty;
    lz->cnt++;
    oldnext = lz->next;
    /* The (size_t) cast below avoids the danger of undefined
       behaviour from signed integer overflow. */
    lz->next += (size_t)lz->step;
    if (lz->next < oldnext || (stop != -1 && lz->next > stop))
        lz->next = stop;
    return item;

empty:
    Py_CLEAR(lz->it);
    return NULL;
}

/*[clinic input]
itertools.islice.__reduce__ as islice_reduce

    lz: self(type="isliceobject *")

Return state information for pickling.
[clinic start generated code]*/

static PyObject *
islice_reduce_impl(isliceobject *lz)
/*[clinic end generated code: output=17608d57ce05ea0f input=2f71fe7c46f200f2]*/
{
    /* When unpickled, generate a new object with the same bounds,
     * then 'setstate' with the next and count
     */
    PyObject *stop;

    if (lz->it == NULL) {
        PyObject *empty_list;
        PyObject *empty_it;
        empty_list = PyList_New(0);
        if (empty_list == NULL)
            return NULL;
        empty_it = PyObject_GetIter(empty_list);
        Py_DECREF(empty_list);
        if (empty_it == NULL)
            return NULL;
        return Py_BuildValue("O(Nn)n", Py_TYPE(lz), empty_it, 0, 0);
    }
    if (lz->stop == -1) {
        stop = Py_None;
        Py_INCREF(stop);
    } else {
        stop = PyLong_FromSsize_t(lz->stop);
        if (stop == NULL)
            return NULL;
    }
    return Py_BuildValue("O(OnNn)n", Py_TYPE(lz),
        lz->it, lz->next, stop, lz->step,
        lz->cnt);
}

/*[clinic input]
itertools.islice.__setstate__ as islice_setstate

    lz: self(type="isliceobject *")
    state: object
    /

Set state information for unpickling.
[clinic start generated code]*/

static PyObject *
islice_setstate(isliceobject *lz, PyObject *state)
/*[clinic end generated code: output=9d7a4479159c852e input=1e1a67c001a84c75]*/
{
    Py_ssize_t cnt = PyLong_AsSsize_t(state);

    if (cnt == -1 && PyErr_Occurred())
        return NULL;
    lz->cnt = cnt;
    Py_RETURN_NONE;
}

static PyMethodDef islice_methods[] = {
    ISLICE_REDUCE_METHODDEF
    ISLICE_SETSTATE_METHODDEF
    {NULL,              NULL}   /* sentinel */
};

PyDoc_STRVAR(islice_doc,
"islice(iterable, stop) --> islice object\n\
islice(iterable, start, stop[, step]) --> islice object\n\
\n\
Return an iterator whose next() method returns selected values from an\n\
iterable.  If start is specified, will skip all preceding elements;\n\
otherwise, start defaults to zero.  Step defaults to one.  If\n\
specified as another value, step determines how many values are \n\
skipped between successive calls.  Works like a slice() on a list\n\
but returns an iterator.");

static PyTypeObject islice_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.islice",                 /* tp_name */
    sizeof(isliceobject),               /* tp_basicsize */
    0,                                  /* tp_itemsize */
    /* methods */
    (destructor)islice_dealloc,         /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_reserved */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    0,                                  /* tp_call */
    0,                                  /* tp_str */
    PyObject_GenericGetAttr,            /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,            /* tp_flags */
    islice_doc,                         /* tp_doc */
    (traverseproc)islice_traverse,      /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    PyObject_SelfIter,                  /* tp_iter */
    (iternextfunc)islice_next,          /* tp_iternext */
    islice_methods,                     /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    islice_new,                         /* tp_new */
    PyObject_GC_Del,                    /* tp_free */
};


/* starmap object ************************************************************/

typedef struct {
    PyObject_HEAD
    PyObject *func;
    PyObject *it;
} starmapobject;

static PyTypeObject starmap_type;

/*[clinic input]
@classmethod
itertools.starmap.__new__ as starmap_new

    function: object
    iterable: object

Create a starmap object.

Return an iterator whose values are returned from the function evaluated
with a argument tuple taken from the given iterable.
[clinic start generated code]*/

static PyObject *
starmap_new_impl(PyTypeObject *type, PyObject *function, PyObject *iterable)
/*[clinic end generated code: output=18acf452cb6c526c input=579f9b3e0229a194]*/
{
    PyObject *it;
    starmapobject *lz;

    /* Get iterator. */
    it = PyObject_GetIter(iterable);
    if (it == NULL)
        return NULL;

    /* create starmapobject structure */
    lz = (starmapobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_DECREF(it);
        return NULL;
    }
    Py_INCREF(function);
    lz->func = function;
    lz->it = it;

    return (PyObject *)lz;
}

static void
starmap_dealloc(starmapobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->func);
    Py_XDECREF(lz->it);
    Py_TYPE(lz)->tp_free(lz);
}

static int
starmap_traverse(starmapobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->it);
    Py_VISIT(lz->func);
    return 0;
}

static PyObject *
starmap_next(starmapobject *lz)
{
    PyObject *args;
    PyObject *result;
    PyObject *it = lz->it;

    args = (*Py_TYPE(it)->tp_iternext)(it);
    if (args == NULL)
        return NULL;
    if (!PyTuple_CheckExact(args)) {
        PyObject *newargs = PySequence_Tuple(args);
        Py_DECREF(args);
        if (newargs == NULL)
            return NULL;
        args = newargs;
    }
    result = PyObject_Call(lz->func, args, NULL);
    Py_DECREF(args);
    return result;
}

/*[clinic input]
itertools.starmap.__reduce__ as starmap_reduce

    lz: self(type="starmapobject *")

Return state information for pickling.
[clinic start generated code]*/

static PyObject *
starmap_reduce_impl(starmapobject *lz)
/*[clinic end generated code: output=f37e06d630220ddd input=8101a8f2910ff54d]*/
{
    /* Just pickle the iterator */
    return Py_BuildValue("O(OO)", Py_TYPE(lz), lz->func, lz->it);
}

static PyMethodDef starmap_methods[] = {
    STARMAP_REDUCE_METHODDEF
    {NULL,              NULL}   /* sentinel */
};

PyDoc_STRVAR(starmap_doc,
"starmap(function, sequence) --> starmap object\n\
\n\
Return an iterator whose values are returned from the function evaluated\n\
with an argument tuple taken from the given sequence.");

static PyTypeObject starmap_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.starmap",                /* tp_name */
    sizeof(starmapobject),              /* tp_basicsize */
    0,                                  /* tp_itemsize */
    /* methods */
    (destructor)starmap_dealloc,        /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_reserved */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    0,                                  /* tp_call */
    0,                                  /* tp_str */
    PyObject_GenericGetAttr,            /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,            /* tp_flags */
    starmap_new__doc__,                 /* tp_doc */
    (traverseproc)starmap_traverse,     /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    PyObject_SelfIter,                  /* tp_iter */
    (iternextfunc)starmap_next,         /* tp_iternext */
    starmap_methods,                    /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    starmap_new,                        /* tp_new */
    PyObject_GC_Del,                    /* tp_free */
};


/* chain object **************************************************************/

typedef struct {
    PyObject_HEAD
    PyObject *source;                   /* Iterator over input iterables */
    PyObject *active;                   /* Currently running input iterator */
} chainobject;

static PyTypeObject chain_type;

static PyObject *
chain_new_internal(PyTypeObject *type, PyObject *source)
{
    chainobject *lz;

    lz = (chainobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_DECREF(source);
        return NULL;
    }

    lz->source = source;
    lz->active = NULL;
    return (PyObject *)lz;
}

static PyObject *
chain_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *source;

    if (type == &chain_type && !_PyArg_NoKeywords("chain", kwds))
        return NULL;

    source = PyObject_GetIter(args);
    if (source == NULL)
        return NULL;

    return chain_new_internal(type, source);
}

/*[clinic input]
@classmethod
itertools.chain.from_iterable as chain_new_from_iterable

    iterable: object
    /

Create a chain object.

Alternate chain() contructor taking a single iterable argument
that evaluates lazily.
[clinic start generated code]*/

static PyObject *
chain_new_from_iterable(PyTypeObject *type, PyObject *iterable)
/*[clinic end generated code: output=2d5fc751e73b5a67 input=1f1c99200abefc48]*/
{
    PyObject *source;

    source = PyObject_GetIter(iterable);
    if (source == NULL)
        return NULL;

    return chain_new_internal(type, source);
}

static void
chain_dealloc(chainobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->active);
    Py_XDECREF(lz->source);
    Py_TYPE(lz)->tp_free(lz);
}

static int
chain_traverse(chainobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->source);
    Py_VISIT(lz->active);
    return 0;
}

static PyObject *
chain_next(chainobject *lz)
{
    PyObject *item;

    /* lz->source is the iterator of iterables. If it's NULL, we've already
     * consumed them all. lz->active is the current iterator. If it's NULL,
     * we should grab a new one from lz->source. */
    while (lz->source != NULL) {
        if (lz->active == NULL) {
            PyObject *iterable = PyIter_Next(lz->source);
            if (iterable == NULL) {
                Py_CLEAR(lz->source);
                return NULL;            /* no more input sources */
            }
            lz->active = PyObject_GetIter(iterable);
            Py_DECREF(iterable);
            if (lz->active == NULL) {
                Py_CLEAR(lz->source);
                return NULL;            /* input not iterable */
            }
        }
        item = (*Py_TYPE(lz->active)->tp_iternext)(lz->active);
        if (item != NULL)
            return item;
        if (PyErr_Occurred()) {
            if (PyErr_ExceptionMatches(PyExc_StopIteration))
                PyErr_Clear();
            else
                return NULL;            /* input raised an exception */
        }
        /* lz->active is consumed, try with the next iterable. */
        Py_CLEAR(lz->active);
    }
    /* Everything had been consumed already. */
    return NULL;
}

/*[clinic input]
itertools.chain.__reduce__ as chain_reduce

    lz: self(type="chainobject *")

Return state information for pickling.
[clinic start generated code]*/

static PyObject *
chain_reduce_impl(chainobject *lz)
/*[clinic end generated code: output=b9cfb60a29f977bc input=3c84dbccd41cefd6]*/
{
    if (lz->source) {
        /* we can't pickle function objects (itertools.from_iterable) so
         * we must use setstate to replace the iterable.  One day we
         * will fix pickling of functions
         */
        if (lz->active) {
            return Py_BuildValue("O()(OO)", Py_TYPE(lz), lz->source, lz->active);
        } else {
            return Py_BuildValue("O()(O)", Py_TYPE(lz), lz->source);
        }
    } else {
        return Py_BuildValue("O()", Py_TYPE(lz)); /* exhausted */
    }
    return NULL;
}

/*[clinic input]
itertools.chain.__setstate__ as chain_setstate

    lz: self(type="chainobject *")
    state: object
    /

Set state information for unpickling.
[clinic start generated code]*/

static PyObject *
chain_setstate(chainobject *lz, PyObject *state)
/*[clinic end generated code: output=5985fde1d79cb81f input=e376ccaff1fb8e28]*/
{
    PyObject *source, *active=NULL;

    if (!PyTuple_Check(state)) {
        PyErr_SetString(PyExc_TypeError, "state is not a tuple");
        return NULL;
    }
    if (!PyArg_ParseTuple(state, "O|O", &source, &active)) {
        return NULL;
    }
    if (!PyIter_Check(source) || (active != NULL && !PyIter_Check(active))) {
        PyErr_SetString(PyExc_TypeError, "Arguments must be iterators.");
        return NULL;
    }

    Py_INCREF(source);
    Py_XSETREF(lz->source, source);
    Py_XINCREF(active);
    Py_XSETREF(lz->active, active);
    Py_RETURN_NONE;
}

PyDoc_STRVAR(chain_doc,
"chain(*iterables) --> chain object\n\
\n\
Return a chain object whose .__next__() method returns elements from the\n\
first iterable until it is exhausted, then elements from the next\n\
iterable, until all of the iterables are exhausted.");

static PyMethodDef chain_methods[] = {
    CHAIN_NEW_FROM_ITERABLE_METHODDEF
    CHAIN_REDUCE_METHODDEF
    CHAIN_SETSTATE_METHODDEF
    {NULL,              NULL}           /* sentinel */
};

static PyTypeObject chain_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.chain",                  /* tp_name */
    sizeof(chainobject),                /* tp_basicsize */
    0,                                  /* tp_itemsize */
    /* methods */
    (destructor)chain_dealloc,          /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_reserved */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    0,                                  /* tp_call */
    0,                                  /* tp_str */
    PyObject_GenericGetAttr,            /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,            /* tp_flags */
    chain_doc,                          /* tp_doc */
    (traverseproc)chain_traverse,       /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    PyObject_SelfIter,                  /* tp_iter */
    (iternextfunc)chain_next,           /* tp_iternext */
    chain_methods,                      /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    chain_new,                          /* tp_new */
    PyObject_GC_Del,                    /* tp_free */
};


/* product object ************************************************************/

typedef struct {
    PyObject_HEAD
    PyObject *pools;        /* tuple of pool tuples */
    Py_ssize_t *indices;    /* one index per pool */
    PyObject *result;       /* most recently returned result tuple */
    int stopped;            /* set to 1 when the iterator is exhausted */
} productobject;

static PyTypeObject product_type;

static PyObject *
product_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    productobject *lz;
    Py_ssize_t nargs, npools, repeat=1;
    PyObject *pools = NULL;
    Py_ssize_t *indices = NULL;
    Py_ssize_t i;

    if (kwds != NULL) {
        char *kwlist[] = {"repeat", 0};
        PyObject *tmpargs = PyTuple_New(0);
        if (tmpargs == NULL)
            return NULL;
        if (!PyArg_ParseTupleAndKeywords(tmpargs, kwds, "|n:product",
                                         kwlist, &repeat)) {
            Py_DECREF(tmpargs);
            return NULL;
        }
        Py_DECREF(tmpargs);
        if (repeat < 0) {
            PyErr_SetString(PyExc_ValueError,
                            "repeat argument cannot be negative");
            return NULL;
        }
    }

    assert(PyTuple_CheckExact(args));
    if (repeat == 0) {
        nargs = 0;
    } else {
        nargs = PyTuple_GET_SIZE(args);
        if ((size_t)nargs > PY_SSIZE_T_MAX/sizeof(Py_ssize_t)/repeat) {
            PyErr_SetString(PyExc_OverflowError, "repeat argument too large");
            return NULL;
        }
    }
    npools = nargs * repeat;

    indices = PyMem_New(Py_ssize_t, npools);
    if (indices == NULL) {
        PyErr_NoMemory();
        goto error;
    }

    pools = PyTuple_New(npools);
    if (pools == NULL)
        goto error;

    for (i=0; i < nargs ; ++i) {
        PyObject *item = PyTuple_GET_ITEM(args, i);
        PyObject *pool = PySequence_Tuple(item);
        if (pool == NULL)
            goto error;
        PyTuple_SET_ITEM(pools, i, pool);
        indices[i] = 0;
    }
    for ( ; i < npools; ++i) {
        PyObject *pool = PyTuple_GET_ITEM(pools, i - nargs);
        Py_INCREF(pool);
        PyTuple_SET_ITEM(pools, i, pool);
        indices[i] = 0;
    }

    /* create productobject structure */
    lz = (productobject *)type->tp_alloc(type, 0);
    if (lz == NULL)
        goto error;

    lz->pools = pools;
    lz->indices = indices;
    lz->result = NULL;
    lz->stopped = 0;

    return (PyObject *)lz;

error:
    if (indices != NULL)
        PyMem_Free(indices);
    Py_XDECREF(pools);
    return NULL;
}

static void
product_dealloc(productobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->pools);
    Py_XDECREF(lz->result);
    if (lz->indices != NULL)
        PyMem_Free(lz->indices);
    Py_TYPE(lz)->tp_free(lz);
}

/*[clinic input]
itertools.product.__sizeof__ as product_sizeof

    lz: self(type="productobject *")

Returns size in memory, in bytes.
[clinic start generated code]*/

static PyObject *
product_sizeof_impl(productobject *lz)
/*[clinic end generated code: output=a943d0f6487dbee1 input=13df894cf42c4ae2]*/
{
    Py_ssize_t res;

    res = _PyObject_SIZE(Py_TYPE(lz));
    res += PyTuple_GET_SIZE(lz->pools) * sizeof(Py_ssize_t);
    return PyLong_FromSsize_t(res);
}

static int
product_traverse(productobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->pools);
    Py_VISIT(lz->result);
    return 0;
}

static PyObject *
product_next(productobject *lz)
{
    PyObject *pool;
    PyObject *elem;
    PyObject *oldelem;
    PyObject *pools = lz->pools;
    PyObject *result = lz->result;
    Py_ssize_t npools = PyTuple_GET_SIZE(pools);
    Py_ssize_t i;

    if (lz->stopped)
        return NULL;

    if (result == NULL) {
        /* On the first pass, return an initial tuple filled with the
           first element from each pool. */
        result = PyTuple_New(npools);
        if (result == NULL)
            goto empty;
        lz->result = result;
        for (i=0; i < npools; i++) {
            pool = PyTuple_GET_ITEM(pools, i);
            if (PyTuple_GET_SIZE(pool) == 0)
                goto empty;
            elem = PyTuple_GET_ITEM(pool, 0);
            Py_INCREF(elem);
            PyTuple_SET_ITEM(result, i, elem);
        }
    } else {
        Py_ssize_t *indices = lz->indices;

        /* Copy the previous result tuple or re-use it if available */
        if (Py_REFCNT(result) > 1) {
            PyObject *old_result = result;
            result = PyTuple_New(npools);
            if (result == NULL)
                goto empty;
            lz->result = result;
            for (i=0; i < npools; i++) {
                elem = PyTuple_GET_ITEM(old_result, i);
                Py_INCREF(elem);
                PyTuple_SET_ITEM(result, i, elem);
            }
            Py_DECREF(old_result);
        }
        /* Now, we've got the only copy so we can update it in-place */
        assert (npools==0 || Py_REFCNT(result) == 1);

        /* Update the pool indices right-to-left.  Only advance to the
           next pool when the previous one rolls-over */
        for (i=npools-1 ; i >= 0 ; i--) {
            pool = PyTuple_GET_ITEM(pools, i);
            indices[i]++;
            if (indices[i] == PyTuple_GET_SIZE(pool)) {
                /* Roll-over and advance to next pool */
                indices[i] = 0;
                elem = PyTuple_GET_ITEM(pool, 0);
                Py_INCREF(elem);
                oldelem = PyTuple_GET_ITEM(result, i);
                PyTuple_SET_ITEM(result, i, elem);
                Py_DECREF(oldelem);
            } else {
                /* No rollover. Just increment and stop here. */
                elem = PyTuple_GET_ITEM(pool, indices[i]);
                Py_INCREF(elem);
                oldelem = PyTuple_GET_ITEM(result, i);
                PyTuple_SET_ITEM(result, i, elem);
                Py_DECREF(oldelem);
                break;
            }
        }

        /* If i is negative, then the indices have all rolled-over
           and we're done. */
        if (i < 0)
            goto empty;
    }

    Py_INCREF(result);
    return result;

empty:
    lz->stopped = 1;
    return NULL;
}

/*[clinic input]
itertools.product.__reduce__ as product_reduce

    lz: self(type="productobject *")

Return state information for pickling.
[clinic start generated code]*/

static PyObject *
product_reduce_impl(productobject *lz)
/*[clinic end generated code: output=780ff23bb8dd0557 input=d20b4bf62464277a]*/
{
    if (lz->stopped) {
        return Py_BuildValue("O(())", Py_TYPE(lz));
    } else if (lz->result == NULL) {
        return Py_BuildValue("OO", Py_TYPE(lz), lz->pools);
    } else {
        PyObject *indices;
        Py_ssize_t n, i;

        /* we must pickle the indices use them for setstate, and
         * additionally indicate that the iterator has started
         */
        n = PyTuple_GET_SIZE(lz->pools);
        indices = PyTuple_New(n);
        if (indices == NULL)
            return NULL;
        for (i=0; i<n; i++){
            PyObject* index = PyLong_FromSsize_t(lz->indices[i]);
            if (!index) {
                Py_DECREF(indices);
                return NULL;
            }
            PyTuple_SET_ITEM(indices, i, index);
        }
        return Py_BuildValue("OON", Py_TYPE(lz), lz->pools, indices);
    }
}

/*[clinic input]
itertools.product.__setstate__ as product_setstate

    lz: self(type="productobject *")
    state: object
    /

Set state information for unpickling.
[clinic start generated code]*/

static PyObject *
product_setstate(productobject *lz, PyObject *state)
/*[clinic end generated code: output=e28c92b80c88e261 input=6e813de246d596e0]*/
{
    PyObject *result;
    Py_ssize_t n, i;

    n = PyTuple_GET_SIZE(lz->pools);
    if (!PyTuple_Check(state) || PyTuple_GET_SIZE(state) != n) {
        PyErr_SetString(PyExc_ValueError, "invalid arguments");
        return NULL;
    }
    for (i=0; i<n; i++)
    {
        PyObject* indexObject = PyTuple_GET_ITEM(state, i);
        Py_ssize_t index = PyLong_AsSsize_t(indexObject);
        PyObject* pool;
        Py_ssize_t poolsize;
        if (index < 0 && PyErr_Occurred())
            return NULL; /* not an integer */
        pool = PyTuple_GET_ITEM(lz->pools, i);
        poolsize = PyTuple_GET_SIZE(pool);
        if (poolsize == 0) {
            lz->stopped = 1;
            Py_RETURN_NONE;
        }
        /* clamp the index */
        if (index < 0)
            index = 0;
        else if (index > poolsize-1)
            index = poolsize-1;
        lz->indices[i] = index;
    }

    result = PyTuple_New(n);
    if (!result)
        return NULL;
    for (i=0; i<n; i++) {
        PyObject *pool = PyTuple_GET_ITEM(lz->pools, i);
        PyObject *element = PyTuple_GET_ITEM(pool, lz->indices[i]);
        Py_INCREF(element);
        PyTuple_SET_ITEM(result, i, element);
    }
    Py_XSETREF(lz->result, result);
    Py_RETURN_NONE;
}

static PyMethodDef product_methods[] = {
    PRODUCT_REDUCE_METHODDEF
    PRODUCT_SETSTATE_METHODDEF
    PRODUCT_SIZEOF_METHODDEF
    {NULL,              NULL}   /* sentinel */
};

PyDoc_STRVAR(product_doc,
"product(*iterables, repeat=1) --> product object\n\
\n\
Cartesian product of input iterables.  Equivalent to nested for-loops.\n\n\
For example, product(A, B) returns the same as:  ((x,y) for x in A for y in B).\n\
The leftmost iterators are in the outermost for-loop, so the output tuples\n\
cycle in a manner similar to an odometer (with the rightmost element changing\n\
on every iteration).\n\n\
To compute the product of an iterable with itself, specify the number\n\
of repetitions with the optional repeat keyword argument. For example,\n\
product(A, repeat=4) means the same as product(A, A, A, A).\n\n\
product('ab', range(3)) --> ('a',0) ('a',1) ('a',2) ('b',0) ('b',1) ('b',2)\n\
product((0,1), (0,1), (0,1)) --> (0,0,0) (0,0,1) (0,1,0) (0,1,1) (1,0,0) ...");

static PyTypeObject product_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.product",                /* tp_name */
    sizeof(productobject),              /* tp_basicsize */
    0,                                  /* tp_itemsize */
    /* methods */
    (destructor)product_dealloc,        /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_reserved */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    0,                                  /* tp_call */
    0,                                  /* tp_str */
    PyObject_GenericGetAttr,            /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,            /* tp_flags */
    product_doc,                        /* tp_doc */
    (traverseproc)product_traverse,     /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    PyObject_SelfIter,                  /* tp_iter */
    (iternextfunc)product_next,         /* tp_iternext */
    product_methods,                    /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    product_new,                        /* tp_new */
    PyObject_GC_Del,                    /* tp_free */
};


/* combinations object *******************************************************/

typedef struct {
    PyObject_HEAD
    PyObject *pool;         /* input converted to a tuple */
    Py_ssize_t *indices;    /* one index per result element */
    PyObject *result;       /* most recently returned result tuple */
    Py_ssize_t r;           /* size of result tuple */
    int stopped;            /* set to 1 when the iterator is exhausted */
} combinationsobject;

static PyTypeObject combinations_type;

/*[clinic input]
@classmethod
itertools.combinations.__new__ as combinations_new

    iterable: object
    r: Py_ssize_t

Create a combinations object.

Returns successive r-length combinations of elements in the iterable.

Example:
combinations(range(4), 3) --> (0,1,2), (0,1,3), (0,2,3), (1,2,3)
[clinic start generated code]*/

static PyObject *
combinations_new_impl(PyTypeObject *type, PyObject *iterable, Py_ssize_t r)
/*[clinic end generated code: output=dcd9df4d7c03311f input=95fa7897cc2fc49c]*/
{
    combinationsobject *co;
    Py_ssize_t n;
    PyObject *pool = NULL;
    Py_ssize_t *indices = NULL;
    Py_ssize_t i;

    pool = PySequence_Tuple(iterable);
    if (pool == NULL)
        goto error;
    n = PyTuple_GET_SIZE(pool);
    if (r < 0) {
        PyErr_SetString(PyExc_ValueError, "r must be non-negative");
        goto error;
    }

    indices = PyMem_New(Py_ssize_t, r);
    if (indices == NULL) {
        PyErr_NoMemory();
        goto error;
    }

    for (i=0 ; i<r ; i++)
        indices[i] = i;

    /* create combinationsobject structure */
    co = (combinationsobject *)type->tp_alloc(type, 0);
    if (co == NULL)
        goto error;

    co->pool = pool;
    co->indices = indices;
    co->result = NULL;
    co->r = r;
    co->stopped = r > n ? 1 : 0;

    return (PyObject *)co;

error:
    if (indices != NULL)
        PyMem_Free(indices);
    Py_XDECREF(pool);
    return NULL;
}

static void
combinations_dealloc(combinationsobject *co)
{
    PyObject_GC_UnTrack(co);
    Py_XDECREF(co->pool);
    Py_XDECREF(co->result);
    if (co->indices != NULL)
        PyMem_Free(co->indices);
    Py_TYPE(co)->tp_free(co);
}

/*[clinic input]
itertools.combinations.__sizeof__ as combinations_sizeof

    co: self(type="combinationsobject *")

Returns size in memory, in bytes.
[clinic start generated code]*/

static PyObject *
combinations_sizeof_impl(combinationsobject *co)
/*[clinic end generated code: output=b5bf6fa9dc59e413 input=aca277f15aec905e]*/
{
    Py_ssize_t res;

    res = _PyObject_SIZE(Py_TYPE(co));
    res += co->r * sizeof(Py_ssize_t);
    return PyLong_FromSsize_t(res);
}

static int
combinations_traverse(combinationsobject *co, visitproc visit, void *arg)
{
    Py_VISIT(co->pool);
    Py_VISIT(co->result);
    return 0;
}

static PyObject *
combinations_next(combinationsobject *co)
{
    PyObject *elem;
    PyObject *oldelem;
    PyObject *pool = co->pool;
    Py_ssize_t *indices = co->indices;
    PyObject *result = co->result;
    Py_ssize_t n = PyTuple_GET_SIZE(pool);
    Py_ssize_t r = co->r;
    Py_ssize_t i, j, index;

    if (co->stopped)
        return NULL;

    if (result == NULL) {
        /* On the first pass, initialize result tuple using the indices */
        result = PyTuple_New(r);
        if (result == NULL)
            goto empty;
        co->result = result;
        for (i=0; i<r ; i++) {
            index = indices[i];
            elem = PyTuple_GET_ITEM(pool, index);
            Py_INCREF(elem);
            PyTuple_SET_ITEM(result, i, elem);
        }
    } else {
        /* Copy the previous result tuple or re-use it if available */
        if (Py_REFCNT(result) > 1) {
            PyObject *old_result = result;
            result = PyTuple_New(r);
            if (result == NULL)
                goto empty;
            co->result = result;
            for (i=0; i<r ; i++) {
                elem = PyTuple_GET_ITEM(old_result, i);
                Py_INCREF(elem);
                PyTuple_SET_ITEM(result, i, elem);
            }
            Py_DECREF(old_result);
        }
        /* Now, we've got the only copy so we can update it in-place
         * CPython's empty tuple is a singleton and cached in
         * PyTuple's freelist.
         */
        assert(r == 0 || Py_REFCNT(result) == 1);

        /* Scan indices right-to-left until finding one that is not
           at its maximum (i + n - r). */
        for (i=r-1 ; i >= 0 && indices[i] == i+n-r ; i--)
            ;

        /* If i is negative, then the indices are all at
           their maximum value and we're done. */
        if (i < 0)
            goto empty;

        /* Increment the current index which we know is not at its
           maximum.  Then move back to the right setting each index
           to its lowest possible value (one higher than the index
           to its left -- this maintains the sort order invariant). */
        indices[i]++;
        for (j=i+1 ; j<r ; j++)
            indices[j] = indices[j-1] + 1;

        /* Update the result tuple for the new indices
           starting with i, the leftmost index that changed */
        for ( ; i<r ; i++) {
            index = indices[i];
            elem = PyTuple_GET_ITEM(pool, index);
            Py_INCREF(elem);
            oldelem = PyTuple_GET_ITEM(result, i);
            PyTuple_SET_ITEM(result, i, elem);
            Py_DECREF(oldelem);
        }
    }

    Py_INCREF(result);
    return result;

empty:
    co->stopped = 1;
    return NULL;
}

/*[clinic input]
itertools.combinations.__reduce__ as combinations_reduce

    lz: self(type="combinationsobject *")

Return state information for pickling.
[clinic start generated code]*/

static PyObject *
combinations_reduce_impl(combinationsobject *lz)
/*[clinic end generated code: output=2dbd90d424ca2a58 input=85cd3171e4f80093]*/
{
    if (lz->result == NULL) {
        return Py_BuildValue("O(On)", Py_TYPE(lz), lz->pool, lz->r);
    } else if (lz->stopped) {
        return Py_BuildValue("O(()n)", Py_TYPE(lz), lz->r);
    } else {
        PyObject *indices;
        Py_ssize_t i;

        /* we must pickle the indices and use them for setstate */
        indices = PyTuple_New(lz->r);
        if (!indices)
            return NULL;
        for (i=0; i<lz->r; i++)
        {
            PyObject* index = PyLong_FromSsize_t(lz->indices[i]);
            if (!index) {
                Py_DECREF(indices);
                return NULL;
            }
            PyTuple_SET_ITEM(indices, i, index);
        }

        return Py_BuildValue("O(On)N", Py_TYPE(lz), lz->pool, lz->r, indices);
    }
}

/*[clinic input]
itertools.combinations.__setstate__ as combinations_setstate

    lz: self(type="combinationsobject *")
    state: object
    /

Set state information for unpickling.
[clinic start generated code]*/

static PyObject *
combinations_setstate(combinationsobject *lz, PyObject *state)
/*[clinic end generated code: output=07388313134a205e input=0e3f5c7702c3f205]*/
{
    PyObject *result;
    Py_ssize_t i;
    Py_ssize_t n = PyTuple_GET_SIZE(lz->pool);

    if (!PyTuple_Check(state) || PyTuple_GET_SIZE(state) != lz->r) {
        PyErr_SetString(PyExc_ValueError, "invalid arguments");
        return NULL;
    }

    for (i=0; i<lz->r; i++) {
        Py_ssize_t max;
        PyObject* indexObject = PyTuple_GET_ITEM(state, i);
        Py_ssize_t index = PyLong_AsSsize_t(indexObject);

        if (index == -1 && PyErr_Occurred())
            return NULL; /* not an integer */
        max = i + n - lz->r;
        /* clamp the index (beware of negative max) */
        if (index > max)
            index = max;
        if (index < 0)
            index = 0;
        lz->indices[i] = index;
    }

    result = PyTuple_New(lz->r);
    if (result == NULL)
        return NULL;
    for (i=0; i<lz->r; i++) {
        PyObject *element = PyTuple_GET_ITEM(lz->pool, lz->indices[i]);
        Py_INCREF(element);
        PyTuple_SET_ITEM(result, i, element);
    }

    Py_XSETREF(lz->result, result);
    Py_RETURN_NONE;
}

static PyMethodDef combinations_methods[] = {
    COMBINATIONS_REDUCE_METHODDEF
    COMBINATIONS_SETSTATE_METHODDEF
    COMBINATIONS_SIZEOF_METHODDEF
    {NULL,              NULL}   /* sentinel */
};

static PyTypeObject combinations_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.combinations",           /* tp_name */
    sizeof(combinationsobject),         /* tp_basicsize */
    0,                                  /* tp_itemsize */
    /* methods */
    (destructor)combinations_dealloc,   /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_reserved */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    0,                                  /* tp_call */
    0,                                  /* tp_str */
    PyObject_GenericGetAttr,            /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,            /* tp_flags */
    combinations_new__doc__,            /* tp_doc */
    (traverseproc)combinations_traverse,/* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    PyObject_SelfIter,                  /* tp_iter */
    (iternextfunc)combinations_next,    /* tp_iternext */
    combinations_methods,               /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    combinations_new,                   /* tp_new */
    PyObject_GC_Del,                    /* tp_free */
};


/* combinations with replacement object **************************************/

/* Equivalent to:

        def combinations_with_replacement(iterable, r):
            "combinations_with_replacement('ABC', 2) --> AA AB AC BB BC CC"
            # number items returned:  (n+r-1)! / r! / (n-1)!
            pool = tuple(iterable)
            n = len(pool)
            indices = [0] * r
            yield tuple(pool[i] for i in indices)
            while 1:
                for i in reversed(range(r)):
                    if indices[i] != n - 1:
                        break
                else:
                    return
                indices[i:] = [indices[i] + 1] * (r - i)
                yield tuple(pool[i] for i in indices)

        def combinations_with_replacement2(iterable, r):
            'Alternate version that filters from product()'
            pool = tuple(iterable)
            n = len(pool)
            for indices in product(range(n), repeat=r):
                if sorted(indices) == list(indices):
                    yield tuple(pool[i] for i in indices)
*/
typedef struct {
    PyObject_HEAD
    PyObject *pool;         /* input converted to a tuple */
    Py_ssize_t *indices;    /* one index per result element */
    PyObject *result;       /* most recently returned result tuple */
    Py_ssize_t r;           /* size of result tuple */
    int stopped;            /* set to 1 when the cwr iterator is exhausted */
} cwrobject;

static PyTypeObject cwr_type;

/*[clinic input]
@classmethod
itertools.combinations_with_replacement.__new__ as cwr_new

    iterable: object
    r: Py_ssize_t

Create a combinations object.

Returns successive r-length combinations of elements in the iterable allowing
individual elements to have successive repeats.

Example:
combinations_with_replacement('ABC', 2) --> AA AB AC BB BC CC
[clinic start generated code]*/

static PyObject *
cwr_new_impl(PyTypeObject *type, PyObject *iterable, Py_ssize_t r)
/*[clinic end generated code: output=14e1057a23897a3f input=9f05a18553d8ae6b]*/
{
    cwrobject *co;
    Py_ssize_t n;
    PyObject *pool = NULL;
    Py_ssize_t *indices = NULL;
    Py_ssize_t i;
    static char *kwargs[] = {"iterable", "r", NULL};

    pool = PySequence_Tuple(iterable);
    if (pool == NULL)
        goto error;
    n = PyTuple_GET_SIZE(pool);
    if (r < 0) {
        PyErr_SetString(PyExc_ValueError, "r must be non-negative");
        goto error;
    }

    indices = PyMem_New(Py_ssize_t, r);
    if (indices == NULL) {
        PyErr_NoMemory();
        goto error;
    }

    for (i=0 ; i<r ; i++)
        indices[i] = 0;

    /* create cwrobject structure */
    co = (cwrobject *)type->tp_alloc(type, 0);
    if (co == NULL)
        goto error;

    co->pool = pool;
    co->indices = indices;
    co->result = NULL;
    co->r = r;
    co->stopped = !n && r;

    return (PyObject *)co;

error:
    if (indices != NULL)
        PyMem_Free(indices);
    Py_XDECREF(pool);
    return NULL;
}

static void
cwr_dealloc(cwrobject *co)
{
    PyObject_GC_UnTrack(co);
    Py_XDECREF(co->pool);
    Py_XDECREF(co->result);
    if (co->indices != NULL)
        PyMem_Free(co->indices);
    Py_TYPE(co)->tp_free(co);
}

/*[clinic input]
itertools.combinations_with_replacement.__sizeof__ as cwr_sizeof

    co: self(type="cwrobject *")

Returns size in memory, in bytes.
[clinic start generated code]*/

static PyObject *
cwr_sizeof_impl(cwrobject *co)
/*[clinic end generated code: output=fa9aa8e2438d6659 input=140461199e63fd20]*/
{
    Py_ssize_t res;

    res = _PyObject_SIZE(Py_TYPE(co));
    res += co->r * sizeof(Py_ssize_t);
    return PyLong_FromSsize_t(res);
}

static int
cwr_traverse(cwrobject *co, visitproc visit, void *arg)
{
    Py_VISIT(co->pool);
    Py_VISIT(co->result);
    return 0;
}

static PyObject *
cwr_next(cwrobject *co)
{
    PyObject *elem;
    PyObject *oldelem;
    PyObject *pool = co->pool;
    Py_ssize_t *indices = co->indices;
    PyObject *result = co->result;
    Py_ssize_t n = PyTuple_GET_SIZE(pool);
    Py_ssize_t r = co->r;
    Py_ssize_t i, index;

    if (co->stopped)
        return NULL;

    if (result == NULL) {
        /* On the first pass, initialize result tuple with pool[0] */
        result = PyTuple_New(r);
        if (result == NULL)
            goto empty;
        co->result = result;
        if (n > 0) {
            elem = PyTuple_GET_ITEM(pool, 0);
            for (i=0; i<r ; i++) {
                assert(indices[i] == 0);
                Py_INCREF(elem);
                PyTuple_SET_ITEM(result, i, elem);
            }
        }
    } else {
        /* Copy the previous result tuple or re-use it if available */
        if (Py_REFCNT(result) > 1) {
            PyObject *old_result = result;
            result = PyTuple_New(r);
            if (result == NULL)
                goto empty;
            co->result = result;
            for (i=0; i<r ; i++) {
                elem = PyTuple_GET_ITEM(old_result, i);
                Py_INCREF(elem);
                PyTuple_SET_ITEM(result, i, elem);
            }
            Py_DECREF(old_result);
        }
        /* Now, we've got the only copy so we can update it in-place CPython's
           empty tuple is a singleton and cached in PyTuple's freelist. */
        assert(r == 0 || Py_REFCNT(result) == 1);

       /* Scan indices right-to-left until finding one that is not
        * at its maximum (n-1). */
        for (i=r-1 ; i >= 0 && indices[i] == n-1; i--)
            ;

        /* If i is negative, then the indices are all at
           their maximum value and we're done. */
        if (i < 0)
            goto empty;

        /* Increment the current index which we know is not at its
           maximum.  Then set all to the right to the same value. */
        index = indices[i] + 1;
        assert(index < n);
        elem = PyTuple_GET_ITEM(pool, index);
        for ( ; i<r ; i++) {
            indices[i] = index;
            Py_INCREF(elem);
            oldelem = PyTuple_GET_ITEM(result, i);
            PyTuple_SET_ITEM(result, i, elem);
            Py_DECREF(oldelem);
        }
    }

    Py_INCREF(result);
    return result;

empty:
    co->stopped = 1;
    return NULL;
}

/*[clinic input]
itertools.combinations_with_replacement.__reduce__ as cwr_reduce

    lz: self(type="cwrobject *")

Return state information for pickling.
[clinic start generated code]*/

static PyObject *
cwr_reduce_impl(cwrobject *lz)
/*[clinic end generated code: output=f20771ee5c58bf7b input=699bd8614edb3264]*/
{
    if (lz->result == NULL) {
        return Py_BuildValue("O(On)", Py_TYPE(lz), lz->pool, lz->r);
    } else if (lz->stopped) {
        return Py_BuildValue("O(()n)", Py_TYPE(lz), lz->r);
    } else {
        PyObject *indices;
        Py_ssize_t i;

        /* we must pickle the indices and use them for setstate */
        indices = PyTuple_New(lz->r);
        if (!indices)
            return NULL;
        for (i=0; i<lz->r; i++) {
            PyObject* index = PyLong_FromSsize_t(lz->indices[i]);
            if (!index) {
                Py_DECREF(indices);
                return NULL;
            }
            PyTuple_SET_ITEM(indices, i, index);
        }

        return Py_BuildValue("O(On)N", Py_TYPE(lz), lz->pool, lz->r, indices);
    }
}

/*[clinic input]
itertools.combinations_with_replacement.__setstate__ as cwr_setstate

    lz: self(type="cwrobject *")
    state: object
    /

Set state information for unpickling.
[clinic start generated code]*/

static PyObject *
cwr_setstate(cwrobject *lz, PyObject *state)
/*[clinic end generated code: output=6b99738e81d51f65 input=f95a94fba268902e]*/
{
    PyObject *result;
    Py_ssize_t n, i;

    if (!PyTuple_Check(state) || PyTuple_GET_SIZE(state) != lz->r)
    {
        PyErr_SetString(PyExc_ValueError, "invalid arguments");
        return NULL;
    }

    n = PyTuple_GET_SIZE(lz->pool);
    for (i=0; i<lz->r; i++) {
        PyObject* indexObject = PyTuple_GET_ITEM(state, i);
        Py_ssize_t index = PyLong_AsSsize_t(indexObject);

        if (index < 0 && PyErr_Occurred())
            return NULL; /* not an integer */
        /* clamp the index */
        if (index < 0)
            index = 0;
        else if (index > n-1)
            index = n-1;
        lz->indices[i] = index;
    }
    result = PyTuple_New(lz->r);
    if (result == NULL)
        return NULL;
    for (i=0; i<lz->r; i++) {
        PyObject *element = PyTuple_GET_ITEM(lz->pool, lz->indices[i]);
        Py_INCREF(element);
        PyTuple_SET_ITEM(result, i, element);
    }
    Py_XSETREF(lz->result, result);
    Py_RETURN_NONE;
}

static PyMethodDef cwr_methods[] = {
    CWR_REDUCE_METHODDEF
    CWR_SETSTATE_METHODDEF
    CWR_SIZEOF_METHODDEF
    {NULL,              NULL}   /* sentinel */
};

static PyTypeObject cwr_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.combinations_with_replacement",          /* tp_name */
    sizeof(cwrobject),                                  /* tp_basicsize */
    0,                                                  /* tp_itemsize */
    /* methods */
    (destructor)cwr_dealloc,                            /* tp_dealloc */
    0,                                                  /* tp_print */
    0,                                                  /* tp_getattr */
    0,                                                  /* tp_setattr */
    0,                                                  /* tp_reserved */
    0,                                                  /* tp_repr */
    0,                                                  /* tp_as_number */
    0,                                                  /* tp_as_sequence */
    0,                                                  /* tp_as_mapping */
    0,                                                  /* tp_hash */
    0,                                                  /* tp_call */
    0,                                                  /* tp_str */
    PyObject_GenericGetAttr,                            /* tp_getattro */
    0,                                                  /* tp_setattro */
    0,                                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,                            /* tp_flags */
    cwr_new__doc__,                                     /* tp_doc */
    (traverseproc)cwr_traverse,                         /* tp_traverse */
    0,                                                  /* tp_clear */
    0,                                                  /* tp_richcompare */
    0,                                                  /* tp_weaklistoffset */
    PyObject_SelfIter,                                  /* tp_iter */
    (iternextfunc)cwr_next,                             /* tp_iternext */
    cwr_methods,                                        /* tp_methods */
    0,                                                  /* tp_members */
    0,                                                  /* tp_getset */
    0,                                                  /* tp_base */
    0,                                                  /* tp_dict */
    0,                                                  /* tp_descr_get */
    0,                                                  /* tp_descr_set */
    0,                                                  /* tp_dictoffset */
    0,                                                  /* tp_init */
    0,                                                  /* tp_alloc */
    cwr_new,                                            /* tp_new */
    PyObject_GC_Del,                                    /* tp_free */
};


/* permutations object ********************************************************

def permutations(iterable, r=None):
    'permutations(range(3), 2) --> (0,1) (0,2) (1,0) (1,2) (2,0) (2,1)'
    pool = tuple(iterable)
    n = len(pool)
    r = n if r is None else r
    indices = range(n)
    cycles = range(n-r+1, n+1)[::-1]
    yield tuple(pool[i] for i in indices[:r])
    while n:
        for i in reversed(range(r)):
            cycles[i] -= 1
            if cycles[i] == 0:
                indices[i:] = indices[i+1:] + indices[i:i+1]
                cycles[i] = n - i
            else:
                j = cycles[i]
                indices[i], indices[-j] = indices[-j], indices[i]
                yield tuple(pool[i] for i in indices[:r])
                break
        else:
            return
*/

typedef struct {
    PyObject_HEAD
    PyObject *pool;         /* input converted to a tuple */
    Py_ssize_t *indices;    /* one index per element in the pool */
    Py_ssize_t *cycles;     /* one rollover counter per element in the result */
    PyObject *result;       /* most recently returned result tuple */
    Py_ssize_t r;           /* size of result tuple */
    int stopped;            /* set to 1 when the iterator is exhausted */
} permutationsobject;

static PyTypeObject permutations_type;

/*[clinic input]
@classmethod
itertools.permutations.__new__ as permutations_new

    iterable: object
    r as robj: object = None

Create a permutations object.

Returns successive r-length permutations of elements in the iterable.

Example:
permutations(range(3), 2) --> (0,1), (0,2), (1,0), (1,2), (2,0), (2,1)
[clinic start generated code]*/

static PyObject *
permutations_new_impl(PyTypeObject *type, PyObject *iterable, PyObject *robj)
/*[clinic end generated code: output=582d59fa75ce954a input=13f00959831dc55e]*/
{
    permutationsobject *po;
    Py_ssize_t n;
    Py_ssize_t r;
    PyObject *pool = NULL;
    Py_ssize_t *indices = NULL;
    Py_ssize_t *cycles = NULL;
    Py_ssize_t i;

    pool = PySequence_Tuple(iterable);
    if (pool == NULL)
        goto error;
    n = PyTuple_GET_SIZE(pool);

    r = n;
    if (robj != Py_None) {
        if (!PyLong_Check(robj)) {
            PyErr_SetString(PyExc_TypeError, "Expected int as r");
            goto error;
        }
        r = PyLong_AsSsize_t(robj);
        if (r == -1 && PyErr_Occurred())
            goto error;
    }
    if (r < 0) {
        PyErr_SetString(PyExc_ValueError, "r must be non-negative");
        goto error;
    }

    indices = PyMem_New(Py_ssize_t, n);
    cycles = PyMem_New(Py_ssize_t, r);
    if (indices == NULL || cycles == NULL) {
        PyErr_NoMemory();
        goto error;
    }

    for (i=0 ; i<n ; i++)
        indices[i] = i;
    for (i=0 ; i<r ; i++)
        cycles[i] = n - i;

    /* create permutationsobject structure */
    po = (permutationsobject *)type->tp_alloc(type, 0);
    if (po == NULL)
        goto error;

    po->pool = pool;
    po->indices = indices;
    po->cycles = cycles;
    po->result = NULL;
    po->r = r;
    po->stopped = r > n ? 1 : 0;

    return (PyObject *)po;

error:
    if (indices != NULL)
        PyMem_Free(indices);
    if (cycles != NULL)
        PyMem_Free(cycles);
    Py_XDECREF(pool);
    return NULL;
}

static void
permutations_dealloc(permutationsobject *po)
{
    PyObject_GC_UnTrack(po);
    Py_XDECREF(po->pool);
    Py_XDECREF(po->result);
    PyMem_Free(po->indices);
    PyMem_Free(po->cycles);
    Py_TYPE(po)->tp_free(po);
}

/*[clinic input]
itertools.permutations.__sizeof__ as permutations_sizeof

    po: self(type="permutationsobject *")

Returns size in memory, in bytes.
[clinic start generated code]*/

static PyObject *
permutations_sizeof_impl(permutationsobject *po)
/*[clinic end generated code: output=b7e8c75b02b8e2c2 input=6cc92f69030f504d]*/
{
    Py_ssize_t res;

    res = _PyObject_SIZE(Py_TYPE(po));
    res += PyTuple_GET_SIZE(po->pool) * sizeof(Py_ssize_t);
    res += po->r * sizeof(Py_ssize_t);
    return PyLong_FromSsize_t(res);
}

static int
permutations_traverse(permutationsobject *po, visitproc visit, void *arg)
{
    Py_VISIT(po->pool);
    Py_VISIT(po->result);
    return 0;
}

static PyObject *
permutations_next(permutationsobject *po)
{
    PyObject *elem;
    PyObject *oldelem;
    PyObject *pool = po->pool;
    Py_ssize_t *indices = po->indices;
    Py_ssize_t *cycles = po->cycles;
    PyObject *result = po->result;
    Py_ssize_t n = PyTuple_GET_SIZE(pool);
    Py_ssize_t r = po->r;
    Py_ssize_t i, j, k, index;

    if (po->stopped)
        return NULL;

    if (result == NULL) {
        /* On the first pass, initialize result tuple using the indices */
        result = PyTuple_New(r);
        if (result == NULL)
            goto empty;
        po->result = result;
        for (i=0; i<r ; i++) {
            index = indices[i];
            elem = PyTuple_GET_ITEM(pool, index);
            Py_INCREF(elem);
            PyTuple_SET_ITEM(result, i, elem);
        }
    } else {
        if (n == 0)
            goto empty;

        /* Copy the previous result tuple or re-use it if available */
        if (Py_REFCNT(result) > 1) {
            PyObject *old_result = result;
            result = PyTuple_New(r);
            if (result == NULL)
                goto empty;
            po->result = result;
            for (i=0; i<r ; i++) {
                elem = PyTuple_GET_ITEM(old_result, i);
                Py_INCREF(elem);
                PyTuple_SET_ITEM(result, i, elem);
            }
            Py_DECREF(old_result);
        }
        /* Now, we've got the only copy so we can update it in-place */
        assert(r == 0 || Py_REFCNT(result) == 1);

        /* Decrement rightmost cycle, moving leftward upon zero rollover */
        for (i=r-1 ; i>=0 ; i--) {
            cycles[i] -= 1;
            if (cycles[i] == 0) {
                /* rotatation: indices[i:] = indices[i+1:] + indices[i:i+1] */
                index = indices[i];
                for (j=i ; j<n-1 ; j++)
                    indices[j] = indices[j+1];
                indices[n-1] = index;
                cycles[i] = n - i;
            } else {
                j = cycles[i];
                index = indices[i];
                indices[i] = indices[n-j];
                indices[n-j] = index;

                for (k=i; k<r ; k++) {
                    /* start with i, the leftmost element that changed */
                    /* yield tuple(pool[k] for k in indices[:r]) */
                    index = indices[k];
                    elem = PyTuple_GET_ITEM(pool, index);
                    Py_INCREF(elem);
                    oldelem = PyTuple_GET_ITEM(result, k);
                    PyTuple_SET_ITEM(result, k, elem);
                    Py_DECREF(oldelem);
                }
                break;
            }
        }
        /* If i is negative, then the cycles have all
           rolled-over and we're done. */
        if (i < 0)
            goto empty;
    }
    Py_INCREF(result);
    return result;

empty:
    po->stopped = 1;
    return NULL;
}

/*[clinic input]
itertools.permutations.__reduce__ as permutations_reduce

    po: self(type="permutationsobject *")

Return state information for pickling.
[clinic start generated code]*/

static PyObject *
permutations_reduce_impl(permutationsobject *po)
/*[clinic end generated code: output=02671f3b618ce7da input=bd7744eca6b1b799]*/
{
    if (po->result == NULL) {
        return Py_BuildValue("O(On)", Py_TYPE(po), po->pool, po->r);
    } else if (po->stopped) {
        return Py_BuildValue("O(()n)", Py_TYPE(po), po->r);
    } else {
        PyObject *indices=NULL, *cycles=NULL;
        Py_ssize_t n, i;

        /* we must pickle the indices and cycles and use them for setstate */
        n = PyTuple_GET_SIZE(po->pool);
        indices = PyTuple_New(n);
        if (indices == NULL)
            goto err;
        for (i=0; i<n; i++) {
            PyObject* index = PyLong_FromSsize_t(po->indices[i]);
            if (!index)
                goto err;
            PyTuple_SET_ITEM(indices, i, index);
        }

        cycles = PyTuple_New(po->r);
        if (cycles == NULL)
            goto err;
        for (i=0 ; i<po->r ; i++) {
            PyObject* index = PyLong_FromSsize_t(po->cycles[i]);
            if (!index)
                goto err;
            PyTuple_SET_ITEM(cycles, i, index);
        }
        return Py_BuildValue("O(On)(NN)", Py_TYPE(po),
                             po->pool, po->r,
                             indices, cycles);
    err:
        Py_XDECREF(indices);
        Py_XDECREF(cycles);
        return NULL;
    }
}

/*[clinic input]
itertools.permutations.__setstate__ as permutations_setstate

    po: self(type="permutationsobject *")
    state: object
    /

Set state information for unpickling.
[clinic start generated code]*/

static PyObject *
permutations_setstate(permutationsobject *po, PyObject *state)
/*[clinic end generated code: output=86e8f8575c33f13a input=ecd8953efad338b0]*/
{
    PyObject *indices, *cycles, *result;
    Py_ssize_t n, i;

    if (!PyTuple_Check(state)) {
        PyErr_SetString(PyExc_TypeError, "state is not a tuple");
        return NULL;
    }
    if (!PyArg_ParseTuple(state, "O!O!",
                          &PyTuple_Type, &indices,
                          &PyTuple_Type, &cycles)) {
        return NULL;
    }

    n = PyTuple_GET_SIZE(po->pool);
    if (PyTuple_GET_SIZE(indices) != n || PyTuple_GET_SIZE(cycles) != po->r) {
        PyErr_SetString(PyExc_ValueError, "invalid arguments");
        return NULL;
    }

    for (i=0; i<n; i++) {
        PyObject* indexObject = PyTuple_GET_ITEM(indices, i);
        Py_ssize_t index = PyLong_AsSsize_t(indexObject);
        if (index < 0 && PyErr_Occurred())
            return NULL; /* not an integer */
        /* clamp the index */
        if (index < 0)
            index = 0;
        else if (index > n-1)
            index = n-1;
        po->indices[i] = index;
    }

    for (i=0; i<po->r; i++) {
        PyObject* indexObject = PyTuple_GET_ITEM(cycles, i);
        Py_ssize_t index = PyLong_AsSsize_t(indexObject);
        if (index < 0 && PyErr_Occurred())
            return NULL; /* not an integer */
        if (index < 1)
            index = 1;
        else if (index > n-i)
            index = n-i;
        po->cycles[i] = index;
    }
    result = PyTuple_New(po->r);
    if (result == NULL)
        return NULL;
    for (i=0; i<po->r; i++) {
        PyObject *element = PyTuple_GET_ITEM(po->pool, po->indices[i]);
        Py_INCREF(element);
        PyTuple_SET_ITEM(result, i, element);
    }
    Py_XSETREF(po->result, result);
    Py_RETURN_NONE;
}

static PyMethodDef permuations_methods[] = {
    PERMUTATIONS_REDUCE_METHODDEF
    PERMUTATIONS_SETSTATE_METHODDEF
    PERMUTATIONS_SIZEOF_METHODDEF
    {NULL,              NULL}   /* sentinel */
};

static PyTypeObject permutations_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.permutations",           /* tp_name */
    sizeof(permutationsobject),         /* tp_basicsize */
    0,                                  /* tp_itemsize */
    /* methods */
    (destructor)permutations_dealloc,   /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_reserved */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    0,                                  /* tp_call */
    0,                                  /* tp_str */
    PyObject_GenericGetAttr,            /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,            /* tp_flags */
    permutations_new__doc__,            /* tp_doc */
    (traverseproc)permutations_traverse,/* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    PyObject_SelfIter,                  /* tp_iter */
    (iternextfunc)permutations_next,    /* tp_iternext */
    permuations_methods,                /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    permutations_new,                   /* tp_new */
    PyObject_GC_Del,                    /* tp_free */
};

/* accumulate object ********************************************************/

typedef struct {
    PyObject_HEAD
    PyObject *total;
    PyObject *it;
    PyObject *binop;
} accumulateobject;

static PyTypeObject accumulate_type;

/*[clinic input]
@classmethod
itertools.accumulate.__new__ as accumulate_new

    iterable: object
    func: object = None

Create an accumulate object.

Return series of accumulated sums (or other binary function results).
[clinic start generated code]*/

static PyObject *
accumulate_new_impl(PyTypeObject *type, PyObject *iterable, PyObject *func)
/*[clinic end generated code: output=784ef75e8d15e887 input=12f7c760779995b6]*/
{
    PyObject *it;
    accumulateobject *lz;

    /* Get iterator. */
    it = PyObject_GetIter(iterable);
    if (it == NULL)
        return NULL;

    /* create accumulateobject structure */
    lz = (accumulateobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_DECREF(it);
        return NULL;
    }

    if (func != Py_None) {
        Py_XINCREF(func);
        lz->binop = func;
    }
    lz->total = NULL;
    lz->it = it;
    return (PyObject *)lz;
}

static void
accumulate_dealloc(accumulateobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->binop);
    Py_XDECREF(lz->total);
    Py_XDECREF(lz->it);
    Py_TYPE(lz)->tp_free(lz);
}

static int
accumulate_traverse(accumulateobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->binop);
    Py_VISIT(lz->it);
    Py_VISIT(lz->total);
    return 0;
}

static PyObject *
accumulate_next(accumulateobject *lz)
{
    PyObject *val, *newtotal;

    val = (*Py_TYPE(lz->it)->tp_iternext)(lz->it);
    if (val == NULL)
        return NULL;

    if (lz->total == NULL) {
        Py_INCREF(val);
        lz->total = val;
        return lz->total;
    }

    if (lz->binop == NULL)
        newtotal = PyNumber_Add(lz->total, val);
    else
        newtotal = PyObject_CallFunctionObjArgs(lz->binop, lz->total, val, NULL);
    Py_DECREF(val);
    if (newtotal == NULL)
        return NULL;

    Py_INCREF(newtotal);
    Py_SETREF(lz->total, newtotal);
    return newtotal;
}

/*[clinic input]
itertools.accumulate.__reduce__ as accumulate_reduce

    lz: self(type="accumulateobject *")

Return state information for pickling.
[clinic start generated code]*/

static PyObject *
accumulate_reduce_impl(accumulateobject *lz)
/*[clinic end generated code: output=2283d4373f698981 input=f247f0e80b550470]*/
{
    if (lz->total == Py_None) {
        PyObject *it;

        if (PyType_Ready(&chain_type) < 0)
            return NULL;
        if (PyType_Ready(&islice_type) < 0)
            return NULL;
        it = PyObject_CallFunction((PyObject *)&chain_type, "(O)O",
                                   lz->total, lz->it);
        if (it == NULL)
            return NULL;
        it = PyObject_CallFunction((PyObject *)Py_TYPE(lz), "NO",
                                   it, lz->binop ? lz->binop : Py_None);
        if (it == NULL)
            return NULL;
        return Py_BuildValue("O(NiO)", &islice_type, it, 1, Py_None);
    }
    return Py_BuildValue("O(OO)O", Py_TYPE(lz),
                            lz->it, lz->binop?lz->binop:Py_None,
                            lz->total?lz->total:Py_None);
}

/*[clinic input]
itertools.accumulate.__setstate__ as accumulate_setstate

    lz: self(type="accumulateobject *")
    state: object
    /

Set state information for unpickling.
[clinic start generated code]*/

static PyObject *
accumulate_setstate(accumulateobject *lz, PyObject *state)
/*[clinic end generated code: output=f29ac8c980f53fb5 input=7dfef829558611fc]*/
{
    Py_INCREF(state);
    Py_XSETREF(lz->total, state);
    Py_RETURN_NONE;
}

static PyMethodDef accumulate_methods[] = {
    ACCUMULATE_REDUCE_METHODDEF
    ACCUMULATE_SETSTATE_METHODDEF
    {NULL,              NULL}   /* sentinel */
};

static PyTypeObject accumulate_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.accumulate",             /* tp_name */
    sizeof(accumulateobject),           /* tp_basicsize */
    0,                                  /* tp_itemsize */
    /* methods */
    (destructor)accumulate_dealloc,     /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_reserved */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    0,                                  /* tp_call */
    0,                                  /* tp_str */
    PyObject_GenericGetAttr,            /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,            /* tp_flags */
    accumulate_new__doc__,              /* tp_doc */
    (traverseproc)accumulate_traverse,  /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    PyObject_SelfIter,                  /* tp_iter */
    (iternextfunc)accumulate_next,      /* tp_iternext */
    accumulate_methods,                 /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    accumulate_new,                     /* tp_new */
    PyObject_GC_Del,                    /* tp_free */
};


/* compress object ************************************************************/

/* Equivalent to:

    def compress(data, selectors):
        "compress('ABCDEF', [1,0,1,0,1,1]) --> A C E F"
        return (d for d, s in zip(data, selectors) if s)
*/

typedef struct {
    PyObject_HEAD
    PyObject *data;
    PyObject *selectors;
} compressobject;

static PyTypeObject compress_type;

/*[clinic input]
@classmethod
itertools.compress.__new__ as compress_new

    data: object
    selectors: object

Create a compress object.

Return data elements corresponding to true selector elements.
Forms a shorter iterator from selected data elements using the
selectors to choose the data elements.
[clinic start generated code]*/

static PyObject *
compress_new_impl(PyTypeObject *type, PyObject *data, PyObject *selectors)
/*[clinic end generated code: output=e3726e87f1cf6ba4 input=2d99935f390029a2]*/
{
    PyObject *data_iter=NULL, *selectors_iter=NULL;
    compressobject *lz;

    data_iter = PyObject_GetIter(data);
    if (data_iter == NULL)
        goto fail;
    selectors_iter = PyObject_GetIter(selectors);
    if (selectors_iter == NULL)
        goto fail;

    /* create compressobject structure */
    lz = (compressobject *)type->tp_alloc(type, 0);
    if (lz == NULL)
        goto fail;
    lz->data = data_iter;
    lz->selectors = selectors_iter;
    return (PyObject *)lz;

fail:
    Py_XDECREF(data_iter);
    Py_XDECREF(selectors_iter);
    return NULL;
}

static void
compress_dealloc(compressobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->data);
    Py_XDECREF(lz->selectors);
    Py_TYPE(lz)->tp_free(lz);
}

static int
compress_traverse(compressobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->data);
    Py_VISIT(lz->selectors);
    return 0;
}

static PyObject *
compress_next(compressobject *lz)
{
    PyObject *data = lz->data, *selectors = lz->selectors;
    PyObject *datum, *selector;
    PyObject *(*datanext)(PyObject *) = *Py_TYPE(data)->tp_iternext;
    PyObject *(*selectornext)(PyObject *) = *Py_TYPE(selectors)->tp_iternext;
    int ok;

    while (1) {
        /* Steps:  get datum, get selector, evaluate selector.
           Order is important (to match the pure python version
           in terms of which input gets a chance to raise an
           exception first).
        */

        datum = datanext(data);
        if (datum == NULL)
            return NULL;

        selector = selectornext(selectors);
        if (selector == NULL) {
            Py_DECREF(datum);
            return NULL;
        }

        ok = PyObject_IsTrue(selector);
        Py_DECREF(selector);
        if (ok > 0)
            return datum;
        Py_DECREF(datum);
        if (ok < 0)
            return NULL;
    }
}

/*[clinic input]
itertools.compress.__reduce__ as compress_reduce

    lz: self(type="compressobject *")

Return state information for pickling.
[clinic start generated code]*/

static PyObject *
compress_reduce_impl(compressobject *lz)
/*[clinic end generated code: output=bfc478e270a28115 input=3e864458db0eccf9]*/
{
    return Py_BuildValue("O(OO)", Py_TYPE(lz),
        lz->data, lz->selectors);
}

static PyMethodDef compress_methods[] = {
    COMPRESS_REDUCE_METHODDEF
    {NULL,              NULL}   /* sentinel */
};

static PyTypeObject compress_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.compress",               /* tp_name */
    sizeof(compressobject),             /* tp_basicsize */
    0,                                  /* tp_itemsize */
    /* methods */
    (destructor)compress_dealloc,       /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_reserved */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    0,                                  /* tp_call */
    0,                                  /* tp_str */
    PyObject_GenericGetAttr,            /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,            /* tp_flags */
    compress_new__doc__,                /* tp_doc */
    (traverseproc)compress_traverse,    /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    PyObject_SelfIter,                  /* tp_iter */
    (iternextfunc)compress_next,        /* tp_iternext */
    compress_methods,                   /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    compress_new,                       /* tp_new */
    PyObject_GC_Del,                    /* tp_free */
};


/* filterfalse object ************************************************************/

typedef struct {
    PyObject_HEAD
    PyObject *func;
    PyObject *it;
} filterfalseobject;

static PyTypeObject filterfalse_type;

/*[clinic input]
@classmethod
itertools.filterfalse.__new__ as filterfalse_new

    function: object
    iterable: object
    /

Create a filterfalse object.

Return those items of iterable for which function(item) is false.
If function is None, return the items that are false.
[clinic start generated code]*/

static PyObject *
filterfalse_new_impl(PyTypeObject *type, PyObject *function,
                     PyObject *iterable)
/*[clinic end generated code: output=d8a4c0a668fcea69 input=3ea201239931baaa]*/
{
    PyObject *it;
    filterfalseobject *lz;

    /* Get iterator. */
    it = PyObject_GetIter(iterable);
    if (it == NULL)
        return NULL;

    /* create filterfalseobject structure */
    lz = (filterfalseobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_DECREF(it);
        return NULL;
    }
    Py_INCREF(function);
    lz->func = function;
    lz->it = it;

    return (PyObject *)lz;
}

static void
filterfalse_dealloc(filterfalseobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->func);
    Py_XDECREF(lz->it);
    Py_TYPE(lz)->tp_free(lz);
}

static int
filterfalse_traverse(filterfalseobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->it);
    Py_VISIT(lz->func);
    return 0;
}

static PyObject *
filterfalse_next(filterfalseobject *lz)
{
    PyObject *item;
    PyObject *it = lz->it;
    long ok;
    PyObject *(*iternext)(PyObject *);

    iternext = *Py_TYPE(it)->tp_iternext;
    for (;;) {
        item = iternext(it);
        if (item == NULL)
            return NULL;

        if (lz->func == Py_None || lz->func == (PyObject *)&PyBool_Type) {
            ok = PyObject_IsTrue(item);
        } else {
            PyObject *good;
            good = PyObject_CallFunctionObjArgs(lz->func, item, NULL);
            if (good == NULL) {
                Py_DECREF(item);
                return NULL;
            }
            ok = PyObject_IsTrue(good);
            Py_DECREF(good);
        }
        if (ok == 0)
            return item;
        Py_DECREF(item);
        if (ok < 0)
            return NULL;
    }
}

/*[clinic input]
itertools.filterfalse.__reduce__ as filterfalse_reduce

    lz: self(type="filterfalseobject *")

Return state information for pickling.
[clinic start generated code]*/

static PyObject *
filterfalse_reduce_impl(filterfalseobject *lz)
/*[clinic end generated code: output=ac8620bdd55ddf72 input=2b3852a10df5d06c]*/
{
    return Py_BuildValue("O(OO)", Py_TYPE(lz), lz->func, lz->it);
}

static PyMethodDef filterfalse_methods[] = {
    FILTERFALSE_REDUCE_METHODDEF
    {NULL,              NULL}   /* sentinel */
};

static PyTypeObject filterfalse_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.filterfalse",            /* tp_name */
    sizeof(filterfalseobject),          /* tp_basicsize */
    0,                                  /* tp_itemsize */
    /* methods */
    (destructor)filterfalse_dealloc,    /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_reserved */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    0,                                  /* tp_call */
    0,                                  /* tp_str */
    PyObject_GenericGetAttr,            /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,            /* tp_flags */
    filterfalse_new__doc__,             /* tp_doc */
    (traverseproc)filterfalse_traverse, /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    PyObject_SelfIter,                  /* tp_iter */
    (iternextfunc)filterfalse_next,     /* tp_iternext */
    filterfalse_methods,                /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    filterfalse_new,                    /* tp_new */
    PyObject_GC_Del,                    /* tp_free */
};


/* count object ************************************************************/

typedef struct {
    PyObject_HEAD
    Py_ssize_t cnt;
    PyObject *long_cnt;
    PyObject *long_step;
} countobject;

/* Counting logic and invariants:

fast_mode:  when cnt an integer < PY_SSIZE_T_MAX and no step is specified.

    assert(cnt != PY_SSIZE_T_MAX && long_cnt == NULL && long_step==PyLong(1));
    Advances with:  cnt += 1
    When count hits Y_SSIZE_T_MAX, switch to slow_mode.

slow_mode:  when cnt == PY_SSIZE_T_MAX, step is not int(1), or cnt is a float.

    assert(cnt == PY_SSIZE_T_MAX && long_cnt != NULL && long_step != NULL);
    All counting is done with python objects (no overflows or underflows).
    Advances with:  long_cnt += long_step
    Step may be zero -- effectively a slow version of repeat(cnt).
    Either long_cnt or long_step may be a float, Fraction, or Decimal.
*/

static PyTypeObject count_type;

/*[clinic input]
@classmethod
itertools.count.__new__ as count_new

    start: object = NULL
    step: object = NULL

Create a count object.

Return a count object whose .__next__() method returns consecutive values.
Equivalent to:

    def count(firstval=0, step=1):
        x = firstval
        while 1:
            yield x
            x += step
[clinic start generated code]*/

static PyObject *
count_new_impl(PyTypeObject *type, PyObject *start, PyObject *step)
/*[clinic end generated code: output=1df9fad69d29f4ec input=2576781c3efb35bf]*/
{
    countobject *lz;
    int fast_mode;
    Py_ssize_t cnt = 0;
    long step_long;

    if ((start != NULL && !PyNumber_Check(start)) ||
        (step != NULL && !PyNumber_Check(step))) {
                    PyErr_SetString(PyExc_TypeError, "a number is required");
                    return NULL;
    }

    fast_mode = (start == NULL || PyLong_Check(start)) &&
                (step == NULL || PyLong_Check(step));

    /* If not specified, start defaults to 0 */
    if (start != NULL) {
        if (fast_mode) {
            assert(PyLong_Check(start));
            cnt = PyLong_AsSsize_t(start);
            if (cnt == -1 && PyErr_Occurred()) {
                PyErr_Clear();
                fast_mode = 0;
            }
        }
    } else {
        cnt = 0;
        start = _PyLong_Zero;
    }
    Py_INCREF(start);

    /* If not specified, step defaults to 1 */
    if (step == NULL)
        step = _PyLong_One;
    Py_INCREF(step);

    assert(start != NULL && step != NULL);

    /* Fast mode only works when the step is 1 */
    if (fast_mode) {
        assert(PyLong_Check(step));
        step_long = PyLong_AsLong(step);
        if (step_long != 1) {
            fast_mode = 0;
            if (step_long == -1 && PyErr_Occurred())
                PyErr_Clear();
        }
    }

    if (fast_mode)
        Py_CLEAR(start);
    else
        cnt = PY_SSIZE_T_MAX;

    assert((cnt != PY_SSIZE_T_MAX && start == NULL && fast_mode) ||
           (cnt == PY_SSIZE_T_MAX && start != NULL && !fast_mode));
    assert(!fast_mode ||
           (PyLong_Check(step) && PyLong_AS_LONG(step) == 1));

    /* create countobject structure */
    lz = (countobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_XDECREF(start);
        return NULL;
    }
    lz->cnt = cnt;
    lz->long_cnt = start;
    lz->long_step = step;

    return (PyObject *)lz;
}

static void
count_dealloc(countobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->long_cnt);
    Py_XDECREF(lz->long_step);
    Py_TYPE(lz)->tp_free(lz);
}

static int
count_traverse(countobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->long_cnt);
    Py_VISIT(lz->long_step);
    return 0;
}

static PyObject *
count_nextlong(countobject *lz)
{
    PyObject *long_cnt;
    PyObject *stepped_up;

    long_cnt = lz->long_cnt;
    if (long_cnt == NULL) {
        /* Switch to slow_mode */
        long_cnt = PyLong_FromSsize_t(PY_SSIZE_T_MAX);
        if (long_cnt == NULL)
            return NULL;
    }
    assert(lz->cnt == PY_SSIZE_T_MAX && long_cnt != NULL);

    stepped_up = PyNumber_Add(long_cnt, lz->long_step);
    if (stepped_up == NULL)
        return NULL;
    lz->long_cnt = stepped_up;
    return long_cnt;
}

static PyObject *
count_next(countobject *lz)
{
    if (lz->cnt == PY_SSIZE_T_MAX)
        return count_nextlong(lz);
    return PyLong_FromSsize_t(lz->cnt++);
}

static PyObject *
count_repr(countobject *lz)
{
    if (lz->cnt != PY_SSIZE_T_MAX)
        return PyUnicode_FromFormat("%s(%zd)",
                                    _PyType_Name(Py_TYPE(lz)), lz->cnt);

    if (PyLong_Check(lz->long_step)) {
        long step = PyLong_AsLong(lz->long_step);
        if (step == -1 && PyErr_Occurred()) {
            PyErr_Clear();
        }
        if (step == 1) {
            /* Don't display step when it is an integer equal to 1 */
            return PyUnicode_FromFormat("%s(%R)",
                                        _PyType_Name(Py_TYPE(lz)),
                                        lz->long_cnt);
        }
    }
    return PyUnicode_FromFormat("%s(%R, %R)",
                                _PyType_Name(Py_TYPE(lz)),
                                lz->long_cnt, lz->long_step);
}

/*[clinic input]
itertools.count.__reduce__ as count_reduce

    lz: self(type="countobject *")

Return state information for pickling.
[clinic start generated code]*/

static PyObject *
count_reduce_impl(countobject *lz)
/*[clinic end generated code: output=64fec5d0ca5aa2cd input=8a68e97535b37707]*/
{
    if (lz->cnt == PY_SSIZE_T_MAX)
        return Py_BuildValue("O(OO)", Py_TYPE(lz), lz->long_cnt, lz->long_step);
    return Py_BuildValue("O(n)", Py_TYPE(lz), lz->cnt);
}

static PyMethodDef count_methods[] = {
    COUNT_REDUCE_METHODDEF
    {NULL,              NULL}   /* sentinel */
};

static PyTypeObject count_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.count",                  /* tp_name */
    sizeof(countobject),                /* tp_basicsize */
    0,                                  /* tp_itemsize */
    /* methods */
    (destructor)count_dealloc,          /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_reserved */
    (reprfunc)count_repr,               /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    0,                                  /* tp_call */
    0,                                  /* tp_str */
    PyObject_GenericGetAttr,            /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,            /* tp_flags */
    count_new__doc__,                   /* tp_doc */
    (traverseproc)count_traverse,       /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    PyObject_SelfIter,                  /* tp_iter */
    (iternextfunc)count_next,           /* tp_iternext */
    count_methods,                      /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    count_new,                          /* tp_new */
    PyObject_GC_Del,                    /* tp_free */
};


/* repeat object ************************************************************/

typedef struct {
    PyObject_HEAD
    PyObject *element;
    Py_ssize_t cnt;
} repeatobject;

static PyTypeObject repeat_type;

static PyObject *
repeat_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    repeatobject *ro;
    PyObject *element;
    Py_ssize_t cnt = -1, n_kwds = 0;
    static char *kwargs[] = {"object", "times", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|n:repeat", kwargs,
                                     &element, &cnt))
        return NULL;

    if (kwds != NULL)
        n_kwds = PyDict_GET_SIZE(kwds);
    /* Does user supply times argument? */
    if ((PyTuple_Size(args) + n_kwds == 2) && cnt < 0)
        cnt = 0;

    ro = (repeatobject *)type->tp_alloc(type, 0);
    if (ro == NULL)
        return NULL;
    Py_INCREF(element);
    ro->element = element;
    ro->cnt = cnt;
    return (PyObject *)ro;
}

static void
repeat_dealloc(repeatobject *ro)
{
    PyObject_GC_UnTrack(ro);
    Py_XDECREF(ro->element);
    Py_TYPE(ro)->tp_free(ro);
}

static int
repeat_traverse(repeatobject *ro, visitproc visit, void *arg)
{
    Py_VISIT(ro->element);
    return 0;
}

static PyObject *
repeat_next(repeatobject *ro)
{
    if (ro->cnt == 0)
        return NULL;
    if (ro->cnt > 0)
        ro->cnt--;
    Py_INCREF(ro->element);
    return ro->element;
}

static PyObject *
repeat_repr(repeatobject *ro)
{
    if (ro->cnt == -1)
        return PyUnicode_FromFormat("%s(%R)",
                                    _PyType_Name(Py_TYPE(ro)), ro->element);
    else
        return PyUnicode_FromFormat("%s(%R, %zd)",
                                    _PyType_Name(Py_TYPE(ro)), ro->element,
                                    ro->cnt);
}

/*[clinic input]
itertools.repeat.__length_hint__ as repeat_len

    ro: self(type="repeatobject *")

Private method returning an estimate of len(list(it)).
[clinic start generated code]*/

static PyObject *
repeat_len_impl(repeatobject *ro)
/*[clinic end generated code: output=f910fff81aefe178 input=59a6f9f93682bcaf]*/
{
    if (ro->cnt == -1) {
        PyErr_SetString(PyExc_TypeError, "len() of unsized object");
        return NULL;
    }
    return PyLong_FromSize_t(ro->cnt);
}

/*[clinic input]
itertools.repeat.__reduce__ as repeat_reduce

    ro: self(type="repeatobject *")

Return state information for pickling.
[clinic start generated code]*/

static PyObject *
repeat_reduce_impl(repeatobject *ro)
/*[clinic end generated code: output=8f15345d61cd588e input=4eab78b8fd9130d4]*/
{
    /* unpickle this so that a new repeat iterator is constructed with an
     * object, then call __setstate__ on it to set cnt
     */
    if (ro->cnt >= 0)
        return Py_BuildValue("O(On)", Py_TYPE(ro), ro->element, ro->cnt);
    else
        return Py_BuildValue("O(O)", Py_TYPE(ro), ro->element);
}

static PyMethodDef repeat_methods[] = {
    REPEAT_LEN_METHODDEF
    REPEAT_REDUCE_METHODDEF
    {NULL,              NULL}           /* sentinel */
};

PyDoc_STRVAR(repeat_doc,
"repeat(object [,times]) -> create an iterator which returns the object\n\
for the specified number of times.  If not specified, returns the object\n\
endlessly.");

static PyTypeObject repeat_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.repeat",                 /* tp_name */
    sizeof(repeatobject),               /* tp_basicsize */
    0,                                  /* tp_itemsize */
    /* methods */
    (destructor)repeat_dealloc,         /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_reserved */
    (reprfunc)repeat_repr,              /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    0,                                  /* tp_call */
    0,                                  /* tp_str */
    PyObject_GenericGetAttr,            /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,            /* tp_flags */
    repeat_doc,                         /* tp_doc */
    (traverseproc)repeat_traverse,      /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    PyObject_SelfIter,                  /* tp_iter */
    (iternextfunc)repeat_next,          /* tp_iternext */
    repeat_methods,                     /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    repeat_new,                         /* tp_new */
    PyObject_GC_Del,                    /* tp_free */
};

/* ziplongest object *********************************************************/

typedef struct {
    PyObject_HEAD
    Py_ssize_t tuplesize;
    Py_ssize_t numactive;
    PyObject *ittuple;                  /* tuple of iterators */
    PyObject *result;
    PyObject *fillvalue;
} ziplongestobject;

static PyTypeObject ziplongest_type;

static PyObject *
zip_longest_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    ziplongestobject *lz;
    Py_ssize_t i;
    PyObject *ittuple;  /* tuple of iterators */
    PyObject *result;
    PyObject *fillvalue = Py_None;
    Py_ssize_t tuplesize;

    if (kwds != NULL && PyDict_CheckExact(kwds) && PyDict_GET_SIZE(kwds) > 0) {
        fillvalue = PyDict_GetItemString(kwds, "fillvalue");
        if (fillvalue == NULL || PyDict_GET_SIZE(kwds) > 1) {
            PyErr_SetString(PyExc_TypeError,
                "zip_longest() got an unexpected keyword argument");
            return NULL;
        }
    }

    /* args must be a tuple */
    assert(PyTuple_Check(args));
    tuplesize = PyTuple_GET_SIZE(args);

    /* obtain iterators */
    ittuple = PyTuple_New(tuplesize);
    if (ittuple == NULL)
        return NULL;
    for (i=0; i < tuplesize; i++) {
        PyObject *item = PyTuple_GET_ITEM(args, i);
        PyObject *it = PyObject_GetIter(item);
        if (it == NULL) {
            if (PyErr_ExceptionMatches(PyExc_TypeError))
                PyErr_Format(PyExc_TypeError,
                    "zip_longest argument #%zd must support iteration",
                    i+1);
            Py_DECREF(ittuple);
            return NULL;
        }
        PyTuple_SET_ITEM(ittuple, i, it);
    }

    /* create a result holder */
    result = PyTuple_New(tuplesize);
    if (result == NULL) {
        Py_DECREF(ittuple);
        return NULL;
    }
    for (i=0 ; i < tuplesize ; i++) {
        Py_INCREF(Py_None);
        PyTuple_SET_ITEM(result, i, Py_None);
    }

    /* create ziplongestobject structure */
    lz = (ziplongestobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_DECREF(ittuple);
        Py_DECREF(result);
        return NULL;
    }
    lz->ittuple = ittuple;
    lz->tuplesize = tuplesize;
    lz->numactive = tuplesize;
    lz->result = result;
    Py_INCREF(fillvalue);
    lz->fillvalue = fillvalue;
    return (PyObject *)lz;
}

static void
zip_longest_dealloc(ziplongestobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->ittuple);
    Py_XDECREF(lz->result);
    Py_XDECREF(lz->fillvalue);
    Py_TYPE(lz)->tp_free(lz);
}

static int
zip_longest_traverse(ziplongestobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->ittuple);
    Py_VISIT(lz->result);
    Py_VISIT(lz->fillvalue);
    return 0;
}

static PyObject *
zip_longest_next(ziplongestobject *lz)
{
    Py_ssize_t i;
    Py_ssize_t tuplesize = lz->tuplesize;
    PyObject *result = lz->result;
    PyObject *it;
    PyObject *item;
    PyObject *olditem;

    if (tuplesize == 0)
        return NULL;
    if (lz->numactive == 0)
        return NULL;
    if (Py_REFCNT(result) == 1) {
        Py_INCREF(result);
        for (i=0 ; i < tuplesize ; i++) {
            it = PyTuple_GET_ITEM(lz->ittuple, i);
            if (it == NULL) {
                Py_INCREF(lz->fillvalue);
                item = lz->fillvalue;
            } else {
                item = PyIter_Next(it);
                if (item == NULL) {
                    lz->numactive -= 1;
                    if (lz->numactive == 0 || PyErr_Occurred()) {
                        lz->numactive = 0;
                        Py_DECREF(result);
                        return NULL;
                    } else {
                        Py_INCREF(lz->fillvalue);
                        item = lz->fillvalue;
                        PyTuple_SET_ITEM(lz->ittuple, i, NULL);
                        Py_DECREF(it);
                    }
                }
            }
            olditem = PyTuple_GET_ITEM(result, i);
            PyTuple_SET_ITEM(result, i, item);
            Py_DECREF(olditem);
        }
    } else {
        result = PyTuple_New(tuplesize);
        if (result == NULL)
            return NULL;
        for (i=0 ; i < tuplesize ; i++) {
            it = PyTuple_GET_ITEM(lz->ittuple, i);
            if (it == NULL) {
                Py_INCREF(lz->fillvalue);
                item = lz->fillvalue;
            } else {
                item = PyIter_Next(it);
                if (item == NULL) {
                    lz->numactive -= 1;
                    if (lz->numactive == 0 || PyErr_Occurred()) {
                        lz->numactive = 0;
                        Py_DECREF(result);
                        return NULL;
                    } else {
                        Py_INCREF(lz->fillvalue);
                        item = lz->fillvalue;
                        PyTuple_SET_ITEM(lz->ittuple, i, NULL);
                        Py_DECREF(it);
                    }
                }
            }
            PyTuple_SET_ITEM(result, i, item);
        }
    }
    return result;
}

/*[clinic input]
itertools.zip_longest.__reduce__ as zip_longest_reduce

    lz: self(type="ziplongestobject *")

Return state information for pickling.
[clinic start generated code]*/

static PyObject *
zip_longest_reduce_impl(ziplongestobject *lz)
/*[clinic end generated code: output=561995d1e971a31d input=25269cf781351ff5]*/
{

    /* Create a new tuple with empty sequences where appropriate to pickle.
     * Then use setstate to set the fillvalue
     */
    int i;
    PyObject *args = PyTuple_New(PyTuple_GET_SIZE(lz->ittuple));

    if (args == NULL)
        return NULL;
    for (i=0; i<PyTuple_GET_SIZE(lz->ittuple); i++) {
        PyObject *elem = PyTuple_GET_ITEM(lz->ittuple, i);
        if (elem == NULL) {
            elem = PyTuple_New(0);
            if (elem == NULL) {
                Py_DECREF(args);
                return NULL;
            }
        } else
            Py_INCREF(elem);
        PyTuple_SET_ITEM(args, i, elem);
    }
    return Py_BuildValue("ONO", Py_TYPE(lz), args, lz->fillvalue);
}

/*[clinic input]
itertools.zip_longest.__setstate__ as zip_longest_setstate

    lz: self(type="ziplongestobject *")
    state: object
    /

Set state information for unpickling.
[clinic start generated code]*/

static PyObject *
zip_longest_setstate(ziplongestobject *lz, PyObject *state)
/*[clinic end generated code: output=55d1c40777c57700 input=0f40aa1fb87dd8c4]*/
{
    Py_INCREF(state);
    Py_XSETREF(lz->fillvalue, state);
    Py_RETURN_NONE;
}

static PyMethodDef zip_longest_methods[] = {
    ZIP_LONGEST_REDUCE_METHODDEF
    ZIP_LONGEST_SETSTATE_METHODDEF
    {NULL,              NULL}   /* sentinel */
};

PyDoc_STRVAR(zip_longest_doc,
"zip_longest(iter1 [,iter2 [...]], [fillvalue=None]) --> zip_longest object\n\
\n\
Return a zip_longest object whose .__next__() method returns a tuple where\n\
the i-th element comes from the i-th iterable argument.  The .__next__()\n\
method continues until the longest iterable in the argument sequence\n\
is exhausted and then it raises StopIteration.  When the shorter iterables\n\
are exhausted, the fillvalue is substituted in their place.  The fillvalue\n\
defaults to None or can be specified by a keyword argument.\n\
");

static PyTypeObject ziplongest_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.zip_longest",            /* tp_name */
    sizeof(ziplongestobject),           /* tp_basicsize */
    0,                                  /* tp_itemsize */
    /* methods */
    (destructor)zip_longest_dealloc,    /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_reserved */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    0,                                  /* tp_call */
    0,                                  /* tp_str */
    PyObject_GenericGetAttr,            /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,            /* tp_flags */
    zip_longest_doc,                    /* tp_doc */
    (traverseproc)zip_longest_traverse, /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    PyObject_SelfIter,                  /* tp_iter */
    (iternextfunc)zip_longest_next,     /* tp_iternext */
    zip_longest_methods,                /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    zip_longest_new,                    /* tp_new */
    PyObject_GC_Del,                    /* tp_free */
};

/* module level code ********************************************************/

PyDoc_STRVAR(module_doc,
"Functional tools for creating and using iterators.\n\
\n\
Infinite iterators:\n\
count(start=0, step=1) --> start, start+step, start+2*step, ...\n\
cycle(p) --> p0, p1, ... plast, p0, p1, ...\n\
repeat(elem [,n]) --> elem, elem, elem, ... endlessly or up to n times\n\
\n\
Iterators terminating on the shortest input sequence:\n\
accumulate(p[, func]) --> p0, p0+p1, p0+p1+p2\n\
chain(p, q, ...) --> p0, p1, ... plast, q0, q1, ... \n\
chain.from_iterable([p, q, ...]) --> p0, p1, ... plast, q0, q1, ... \n\
compress(data, selectors) --> (d[0] if s[0]), (d[1] if s[1]), ...\n\
dropwhile(pred, seq) --> seq[n], seq[n+1], starting when pred fails\n\
groupby(iterable[, keyfunc]) --> sub-iterators grouped by value of keyfunc(v)\n\
filterfalse(pred, seq) --> elements of seq where pred(elem) is False\n\
islice(seq, [start,] stop [, step]) --> elements from\n\
       seq[start:stop:step]\n\
starmap(fun, seq) --> fun(*seq[0]), fun(*seq[1]), ...\n\
tee(it, n=2) --> (it1, it2 , ... itn) splits one iterator into n\n\
takewhile(pred, seq) --> seq[0], seq[1], until pred fails\n\
zip_longest(p, q, ...) --> (p[0], q[0]), (p[1], q[1]), ... \n\
\n\
Combinatoric generators:\n\
product(p, q, ... [repeat=1]) --> cartesian product\n\
permutations(p[, r])\n\
combinations(p, r)\n\
combinations_with_replacement(p, r)\n\
");


static PyMethodDef module_methods[] = {
    TEE_METHODDEF
    {NULL,              NULL}           /* sentinel */
};


static struct PyModuleDef itertoolsmodule = {
    PyModuleDef_HEAD_INIT,
    "itertools",
    module_doc,
    -1,
    module_methods,
    NULL,
    NULL,
    NULL,
    NULL
};

PyMODINIT_FUNC
PyInit_itertools(void)
{
    int i;
    PyObject *m;
    const char *name;
    PyTypeObject *typelist[] = {
        &accumulate_type,
        &combinations_type,
        &cwr_type,
        &cycle_type,
        &dropwhile_type,
        &takewhile_type,
        &islice_type,
        &starmap_type,
        &chain_type,
        &compress_type,
        &filterfalse_type,
        &count_type,
        &ziplongest_type,
        &permutations_type,
        &product_type,
        &repeat_type,
        &groupby_type,
        &_grouper_type,
        &tee_type,
        &teedataobject_type,
        NULL
    };

    Py_TYPE(&teedataobject_type) = &PyType_Type;
    m = PyModule_Create(&itertoolsmodule);
    if (m == NULL)
        return NULL;

    for (i=0 ; typelist[i] != NULL ; i++) {
        if (PyType_Ready(typelist[i]) < 0)
            return NULL;
        name = _PyType_Name(typelist[i]);
        Py_INCREF(typelist[i]);
        PyModule_AddObject(m, name, (PyObject *)typelist[i]);
    }

    return m;
}
