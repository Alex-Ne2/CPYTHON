/*[clinic input]
preserve
[clinic start generated code]*/

PyDoc_STRVAR(_queue_SimpleQueue___init____doc__,
"SimpleQueue()\n"
"--\n"
"\n"
"Simple reentrant queue.");

static int
_queue_SimpleQueue___init___impl(simplequeueobject *self);

static int
_queue_SimpleQueue___init__(PyObject *self, PyObject *args, PyObject *kwargs)
{
    int return_value = -1;

    if ((Py_TYPE(self) == &PySimpleQueueType) &&
        !_PyArg_NoPositional("SimpleQueue", args)) {
        goto exit;
    }
    if ((Py_TYPE(self) == &PySimpleQueueType) &&
        !_PyArg_NoKeywords("SimpleQueue", kwargs)) {
        goto exit;
    }
    return_value = _queue_SimpleQueue___init___impl((simplequeueobject *)self);

exit:
    return return_value;
}

PyDoc_STRVAR(_queue_SimpleQueue_put__doc__,
"put($self, /, item)\n"
"--\n"
"\n"
"Put the item on the queue.  This method never blocks.");

#define _QUEUE_SIMPLEQUEUE_PUT_METHODDEF    \
    {"put", (PyCFunction)_queue_SimpleQueue_put, METH_FASTCALL|METH_KEYWORDS, _queue_SimpleQueue_put__doc__},

static PyObject *
_queue_SimpleQueue_put_impl(simplequeueobject *self, PyObject *item);

static PyObject *
_queue_SimpleQueue_put(simplequeueobject *self, PyObject **args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"item", NULL};
    static _PyArg_Parser _parser = {"O:put", _keywords, 0};
    PyObject *item;

    if (!_PyArg_ParseStackAndKeywords(args, nargs, kwnames, &_parser,
        &item)) {
        goto exit;
    }
    return_value = _queue_SimpleQueue_put_impl(self, item);

exit:
    return return_value;
}

PyDoc_STRVAR(_queue_SimpleQueue_get__doc__,
"get($self, /, block=True, timeout=None)\n"
"--\n"
"\n"
"Remove and return an item from the queue.\n"
"\n"
"If optional args \'block\' is true and \'timeout\' is None (the default),\n"
"block if necessary until an item is available. If \'timeout\' is\n"
"a non-negative number, it blocks at most \'timeout\' seconds and raises\n"
"the Empty exception if no item was available within that time.\n"
"Otherwise (\'block\' is false), return an item if one is immediately\n"
"available, else raise the Empty exception (\'timeout\' is ignored\n"
"in that case).");

#define _QUEUE_SIMPLEQUEUE_GET_METHODDEF    \
    {"get", (PyCFunction)_queue_SimpleQueue_get, METH_FASTCALL|METH_KEYWORDS, _queue_SimpleQueue_get__doc__},

static PyObject *
_queue_SimpleQueue_get_impl(simplequeueobject *self, int block,
                            PyObject *timeout);

static PyObject *
_queue_SimpleQueue_get(simplequeueobject *self, PyObject **args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"block", "timeout", NULL};
    static _PyArg_Parser _parser = {"|pO:get", _keywords, 0};
    int block = 1;
    PyObject *timeout = Py_None;

    if (!_PyArg_ParseStackAndKeywords(args, nargs, kwnames, &_parser,
        &block, &timeout)) {
        goto exit;
    }
    return_value = _queue_SimpleQueue_get_impl(self, block, timeout);

exit:
    return return_value;
}

PyDoc_STRVAR(_queue_SimpleQueue_empty__doc__,
"empty($self, /)\n"
"--\n"
"\n"
"Return True if the queue is empty, False otherwise (not reliable!).");

#define _QUEUE_SIMPLEQUEUE_EMPTY_METHODDEF    \
    {"empty", (PyCFunction)_queue_SimpleQueue_empty, METH_NOARGS, _queue_SimpleQueue_empty__doc__},

static PyObject *
_queue_SimpleQueue_empty_impl(simplequeueobject *self);

static PyObject *
_queue_SimpleQueue_empty(simplequeueobject *self, PyObject *Py_UNUSED(ignored))
{
    return _queue_SimpleQueue_empty_impl(self);
}

PyDoc_STRVAR(_queue_SimpleQueue_qsize__doc__,
"qsize($self, /)\n"
"--\n"
"\n"
"Return the approximate size of the queue (not reliable!).");

#define _QUEUE_SIMPLEQUEUE_QSIZE_METHODDEF    \
    {"qsize", (PyCFunction)_queue_SimpleQueue_qsize, METH_NOARGS, _queue_SimpleQueue_qsize__doc__},

static PyObject *
_queue_SimpleQueue_qsize_impl(simplequeueobject *self);

static PyObject *
_queue_SimpleQueue_qsize(simplequeueobject *self, PyObject *Py_UNUSED(ignored))
{
    return _queue_SimpleQueue_qsize_impl(self);
}
/*[clinic end generated code: output=9dbbdf1f531051c2 input=a9049054013a1b77]*/
