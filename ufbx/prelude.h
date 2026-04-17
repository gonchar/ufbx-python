#include "ufbx.h"

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#define array_count(arr) (sizeof(arr) / sizeof(*(arr)))

static PyObject *UfbxError = NULL;
static PyObject *UseAfterFreeError = NULL;
static PyObject *BufferReferenceError = NULL;

#if PY_MINOR_VERSION < 10
static PyObject *_Py_NewRef(PyObject *obj)
{
	Py_XINCREF(obj);
	return obj;
}

#define Py_NewRef(obj) _Py_NewRef((PyObject*)(obj))
#endif

typedef struct {
	PyObject_HEAD
	bool ok;
	PyObject *name;

	ufbx_scene *scene;
	ufbx_anim *anim;
	ufbx_baked_anim *baked;
	ufbx_geometry_cache *cache;

    size_t num_elements;
	PyObject **elements;

	ptrdiff_t buffer_refs;
} Context;

static PyObject *Element_clear_slots(PyObject *elem);

static bool Context_free(Context *self)
{
	if (!self->ok) return true;
	if (self->buffer_refs > 0) {
		PyErr_Format(BufferReferenceError, "%U has %lld buffer references preventing it from unloading",
			self->name, (long long)self->buffer_refs);
		return false;
	}

	self->ok = false;

	for (size_t i = 0; i < self->num_elements; i++) {
		PyObject *obj = self->elements[i];
		if (!obj) continue;

		Element_clear_slots(obj);

		Py_SETREF(self->elements[i], Py_NewRef(Py_None));
	}

	ufbx_free_scene(self->scene);
	ufbx_free_anim(self->anim);
	ufbx_free_baked_anim(self->baked);
	ufbx_free_geometry_cache(self->cache);

	free(self->elements);

	self->scene = NULL;
	self->anim = NULL;
	self->baked = NULL;
	self->cache = NULL;
	self->elements = NULL;
	self->num_elements = 0;
	return true;
}

static int Context_traverse(Context *self, visitproc visit, void *arg)
{
	Py_VISIT(self->name);
	for (size_t i = 0; i < self->num_elements; i++) {
		Py_VISIT(self->elements[i]);
	}
	return 0;
}

static int Context_clear(Context *self)
{
	Py_CLEAR(self->name);
	for (size_t i = 0; i < self->num_elements; i++) {
		Py_CLEAR(self->elements[i]);
	}
	return 0;
}

static void Context_dealloc(Context *self)
{
	// Context_clear (the GC tp_clear slot) may have already run and nulled
	// self->name to break a cycle, so Py_XDECREF rather than Py_DECREF.
	// Untrack from the cyclic GC before freeing anything it might still visit.
	PyObject_GC_UnTrack(self);
	Context_free(self);
	Py_XDECREF(self->name);
	self->name = NULL;

	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject Context_Type = {
    .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "ufbx.Context",
    .tp_doc = PyDoc_STR("Context"),
    .tp_basicsize = sizeof(Context),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_HAVE_GC,
    .tp_new = PyType_GenericNew,
	.tp_dealloc = (destructor)&Context_dealloc,
	.tp_traverse = (traverseproc)&Context_traverse,
	.tp_clear = (inquiry)&Context_clear,
};

static PyObject *Element_create(ufbx_element *elem, Context *ctx);

static PyObject *UfbxError_raise(ufbx_error *error);
static PyObject *Panic_raise(ufbx_panic *panic);

static PyObject *Context_error(Context *ctx)
{
	PyErr_Format(UseAfterFreeError, "%U used after being freed", ctx->name);
	return NULL;
}

static PyObject *Vec2_Type;
static PyObject *Vec3_Type;
static PyObject *Vec4_Type;
static PyObject *Quat_Type;
static PyObject *Matrix_Type;

static PyObject* Vec2_from(const ufbx_vec2 *v)
{
	PyObject *r = PyTuple_New(2);
	if (!r) return NULL;
	PyTuple_SetItem(r, 0, PyFloat_FromDouble(v->x));
	PyTuple_SetItem(r, 1, PyFloat_FromDouble(v->y));
    PyObject *result = PyObject_CallObject(Vec2_Type, r);
    Py_XDECREF(r);
    return result;
}

static PyObject* Vec3_from(const ufbx_vec3 *v)
{
	PyObject *r = PyTuple_New(3);
	if (!r) return NULL;
	PyTuple_SetItem(r, 0, PyFloat_FromDouble(v->x));
	PyTuple_SetItem(r, 1, PyFloat_FromDouble(v->y));
	PyTuple_SetItem(r, 2, PyFloat_FromDouble(v->z));
    PyObject *result = PyObject_CallObject(Vec3_Type, r);
    Py_XDECREF(r);
    return result;
}

static PyObject* Vec4_from(const ufbx_vec4 *v)
{
	PyObject *r = PyTuple_New(4);
	if (!r) return NULL;
	PyTuple_SetItem(r, 0, PyFloat_FromDouble(v->x));
	PyTuple_SetItem(r, 1, PyFloat_FromDouble(v->y));
	PyTuple_SetItem(r, 2, PyFloat_FromDouble(v->z));
	PyTuple_SetItem(r, 3, PyFloat_FromDouble(v->w));
    PyObject *result = PyObject_CallObject(Vec4_Type, r);
    Py_XDECREF(r);
    return result;
}

static PyObject* Quat_from(const ufbx_quat *v)
{
	PyObject *r = PyTuple_New(4);
	if (!r) return NULL;
	PyTuple_SetItem(r, 0, PyFloat_FromDouble(v->x));
	PyTuple_SetItem(r, 1, PyFloat_FromDouble(v->y));
	PyTuple_SetItem(r, 2, PyFloat_FromDouble(v->z));
	PyTuple_SetItem(r, 3, PyFloat_FromDouble(v->w));
    PyObject *result = PyObject_CallObject(Quat_Type, r);
    Py_XDECREF(r);
    return result;
}

static PyObject* Matrix_from(const ufbx_matrix *v)
{
	PyObject *r = PyTuple_New(4);
	if (!r) return NULL;
	PyTuple_SetItem(r, 0, Vec3_from(&v->cols[0]));
	PyTuple_SetItem(r, 1, Vec3_from(&v->cols[1]));
	PyTuple_SetItem(r, 2, Vec3_from(&v->cols[2]));
	PyTuple_SetItem(r, 3, Vec3_from(&v->cols[3]));
    PyObject *result = PyObject_CallObject(Matrix_Type, r);
    Py_XDECREF(r);
    return result;
}

static ufbx_vec2 Vec2_to(PyObject *v)
{
	ufbx_vec2 r;
	r.x = (ufbx_real)PyFloat_AsDouble(PyTuple_GetItem(v, 0));
	r.y = (ufbx_real)PyFloat_AsDouble(PyTuple_GetItem(v, 1));
	return r;
}

static ufbx_vec3 Vec3_to(PyObject *v)
{
	ufbx_vec3 r;
	r.x = (ufbx_real)PyFloat_AsDouble(PyTuple_GetItem(v, 0));
	r.y = (ufbx_real)PyFloat_AsDouble(PyTuple_GetItem(v, 1));
	r.z = (ufbx_real)PyFloat_AsDouble(PyTuple_GetItem(v, 2));
	return r;
}

static ufbx_vec4 Vec4_to(PyObject *v)
{
	ufbx_vec4 r;
	r.x = (ufbx_real)PyFloat_AsDouble(PyTuple_GetItem(v, 0));
	r.y = (ufbx_real)PyFloat_AsDouble(PyTuple_GetItem(v, 1));
	r.z = (ufbx_real)PyFloat_AsDouble(PyTuple_GetItem(v, 2));
	r.w = (ufbx_real)PyFloat_AsDouble(PyTuple_GetItem(v, 3));
	return r;
}

static ufbx_quat Quat_to(PyObject *v)
{
	ufbx_quat r;
	r.x = (ufbx_real)PyFloat_AsDouble(PyTuple_GetItem(v, 0));
	r.y = (ufbx_real)PyFloat_AsDouble(PyTuple_GetItem(v, 1));
	r.z = (ufbx_real)PyFloat_AsDouble(PyTuple_GetItem(v, 2));
	r.w = (ufbx_real)PyFloat_AsDouble(PyTuple_GetItem(v, 3));
	return r;
}

static ufbx_matrix Matrix_to(PyObject *v)
{
	ufbx_matrix r;
	r.cols[0] = Vec3_to(PyTuple_GetItem(v, 0));
	r.cols[1] = Vec3_to(PyTuple_GetItem(v, 1));
	r.cols[2] = Vec3_to(PyTuple_GetItem(v, 2));
	r.cols[3] = Vec3_to(PyTuple_GetItem(v, 3));
	return r;
}

static PyObject* String_from(ufbx_string v)
{
    return PyUnicode_FromStringAndSize(v.data, (Py_ssize_t)v.length);
}

static PyObject* Blob_from(ufbx_blob v)
{
    return PyBytes_FromStringAndSize((const char *)v.data, (Py_ssize_t)v.size);
}

static ufbx_string String_to(PyObject* v)
{
	Py_ssize_t length;
	const char *ptr = PyUnicode_AsUTF8AndSize(v, &length);
	if (!ptr || length < 0) return ufbx_empty_string;

	ufbx_string result;
	result.data = ptr;
	result.length = (size_t)length;
	return result;
}

static ufbx_blob Blob_to(PyObject *v)
{
	Py_ssize_t size = 0;
	char *buffer = NULL;
	if (PyBytes_AsStringAndSize(v, &buffer, &size) < 0) return ufbx_empty_blob;
	if (!buffer || size < 0) return ufbx_empty_blob;

	ufbx_blob result;
	result.data = buffer;
	result.size = (size_t)size;
	return result;
}

static PyObject* Element_from(void *p_elem, Context *ctx)
{
	if (!p_elem) return Py_NewRef(Py_None);
    if (!ctx->ok) return Context_error(ctx);

    ufbx_element *elem = (ufbx_element*)p_elem;
    PyObject **p_existing = &ctx->elements[elem->element_id];
    // Cache owns one strong ref for the lifetime of the Context; every caller
    // gets its own new ref. Previously the cache held a borrowed ref and
    // callers also received borrowed refs, which corrupted the refcount the
    // moment a temporary (e.g. `scene.nodes[i]`) went out of scope: the
    // cached PyObject was freed while `ctx->elements[i]` still pointed at it,
    // and `Context_free` later `Py_SETREF`'d a dangling pointer.
    if (*p_existing) return Py_NewRef(*p_existing);

    PyObject *obj = Element_create(elem, ctx);
    if (!obj) return NULL;
    *p_existing = obj;
    return Py_NewRef(obj);
}

static PyObject* Scene_create(ufbx_scene *scene);
static PyObject* Anim_create(ufbx_anim *anim);
static PyObject* BakedAnim_create(ufbx_baked_anim *baked);
static PyObject* GeometryCache_create(ufbx_geometry_cache *cache);

static PyObject* to_pyobject_todo(const char *type)
{
    PyErr_Format(PyExc_NotImplementedError, "PyObject todo: %s", type);
    return NULL;
}

static PyObject* pyobject_todo(const char *message)
{
    PyErr_Format(PyExc_NotImplementedError, "todo: %s", message);
    return NULL;
}

typedef struct {
	int32_t value;
	const char *name;
} EnumValue;

typedef struct {
	PyObject **p_type;
	const char *name;
} ExternalType;

typedef struct {
	PyTypeObject *type;
	const char *name;
} ModuleType;

typedef struct {
	const char *mod_name;
	const char *name;
} ErrorType;

static ModuleType prelude_types[] = {
	{ &Context_Type, "Context" },
};

static bool register_type(PyObject *m, PyTypeObject *type, const char *name)
{
	if (PyType_Ready(type) < 0) {
		return false;
	}
	if (name) {
		if (PyModule_AddObject(m, name, Py_NewRef(type)) < 0) {
			return false;
		}
	}
	return true;
}

static bool load_external_types(PyObject *mod_types, const ExternalType *exts, size_t num_exts)
{
	for (size_t i = 0; i < num_exts; i++) {
		ExternalType et = exts[i];
		PyObject *obj = PyObject_GetAttrString(mod_types, et.name);
		if (!obj) return false;

		*et.p_type = obj;
	}
	return true;
}

static ExternalType prelude_ext_types[] = {
    { &Vec2_Type, "Vec2" },
    { &Vec3_Type, "Vec3" },
    { &Vec4_Type, "Vec4" },
    { &Quat_Type, "Quat" },
    { &Matrix_Type, "Matrix" },
};
