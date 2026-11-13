#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include "py_ar_hook.h"

static int g_py_active = 0;
static PyObject *g_pModule = NULL;
static PyObject *g_pFunc = NULL;

int py_ar_init(const char *script_path, const char *func_name)
{
    if (g_py_active) return 1;

    const char *path = script_path;
    if (!path || !*path) {
        path = getenv("RTK_PY_AR_HOOK");
    }
    if (!path || !*path) {
        return 0; /* Not configured, hook inactive */
    }

    const char *fn = func_name;
    if (!fn || !*fn) {
        fn = getenv("RTK_PY_AR_FUNC");
    }
    if (!fn || !*fn) {
        fn = "evaluate_ar";
    }

    fprintf(stderr, "[RTKLIB-C-PY-C] Initializing embedded Python AR hook: script=%s, func=%s\n", path, fn);

    if (!Py_IsInitialized()) {
        Py_Initialize();
    }

    /* Extract directory and module name from path */
    char path_buf1[1024];
    char path_buf2[1024];
    strncpy(path_buf1, path, sizeof(path_buf1) - 1);
    strncpy(path_buf2, path, sizeof(path_buf2) - 1);
    path_buf1[sizeof(path_buf1) - 1] = '\0';
    path_buf2[sizeof(path_buf2) - 1] = '\0';

    char *dir_name = dirname(path_buf1);
    char *base_name = basename(path_buf2);

    /* Strip .py extension if present */
    char mod_name[256];
    strncpy(mod_name, base_name, sizeof(mod_name) - 1);
    mod_name[sizeof(mod_name) - 1] = '\0';
    char *dot = strrchr(mod_name, '.');
    if (dot && strcmp(dot, ".py") == 0) {
        *dot = '\0';
    }

    /* Add directory to sys.path */
    PyObject *sys_path = PySys_GetObject("path");
    if (sys_path) {
        PyObject *py_dir = PyUnicode_FromString(dir_name);
        PyList_Insert(sys_path, 0, py_dir);
        Py_DECREF(py_dir);
    }

    /* Import module */
    PyObject *pName = PyUnicode_FromString(mod_name);
    g_pModule = PyImport_Import(pName);
    Py_DECREF(pName);

    if (!g_pModule) {
        fprintf(stderr, "[RTKLIB-C-PY-C] ERROR: Failed to import Python module '%s' from '%s'\n", mod_name, dir_name);
        PyErr_Print();
        return 0;
    }

    g_pFunc = PyObject_GetAttrString(g_pModule, fn);
    if (!g_pFunc || !PyCallable_Check(g_pFunc)) {
        fprintf(stderr, "[RTKLIB-C-PY-C] ERROR: Cannot find callable '%s' in module '%s'\n", fn, mod_name);
        if (PyErr_Occurred()) PyErr_Print();
        Py_XDECREF(g_pFunc);
        g_pFunc = NULL;
        Py_DECREF(g_pModule);
        g_pModule = NULL;
        return 0;
    }

    g_py_active = 1;
    fprintf(stderr, "[RTKLIB-C-PY-C] Embedded Python AR hook activated successfully!\n");
    return 1;
}

int py_ar_is_active(void)
{
    return g_py_active;
}

int py_ar_evaluate(const double *features, int n_feat)
{
    if (!g_py_active || !g_pFunc) {
        return 0;
    }

    PyObject *pArgs = PyTuple_New(n_feat);
    for (int i = 0; i < n_feat; i++) {
        PyTuple_SetItem(pArgs, i, PyFloat_FromDouble(features[i]));
    }

    PyObject *pValue = PyObject_CallObject(g_pFunc, pArgs);
    Py_DECREF(pArgs);

    if (pValue != NULL) {
        long res = PyLong_AsLong(pValue);
        Py_DECREF(pValue);
        return (int)res;
    } else {
        fprintf(stderr, "[RTKLIB-C-PY-C] Exception in Python evaluate_ar()!\n");
        PyErr_Print();
        return 0; /* Safe fallback: reject fix on exception */
    }
}

void py_ar_cleanup(void)
{
    if (g_py_active) {
        Py_XDECREF(g_pFunc);
        g_pFunc = NULL;
        Py_XDECREF(g_pModule);
        g_pModule = NULL;
        if (Py_IsInitialized()) {
            Py_Finalize();
        }
        g_py_active = 0;
        fprintf(stderr, "[RTKLIB-C-PY-C] Python AR hook finalized.\n");
    }
}
