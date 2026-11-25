#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "py_ar_hook.h"

static int g_py_active = 0;
static int g_owns_python = 0;
static PyObject *g_pModule = NULL;
static PyObject *g_pFunc = NULL;

int py_ar_init(const char *script_path, const char *func_name)
{
    const char *path = (script_path && *script_path) ? script_path : getenv("RTK_PY_AR_HOOK");
    const char *fn = (func_name && *func_name) ? func_name : getenv("RTK_PY_AR_FUNC");
    const char *base;
    PyObject *globals, *value = NULL, *init = NULL;
    FILE *stream;

    if (g_py_active) return 1;
    if (!path || !*path) return 0;
    base = strrchr(path, '/');
    if (strcmp(base ? base + 1 : path, "ml_ar_hook.py") ||
        (fn && *fn && strcmp(fn, "evaluate_ar"))) {
        fprintf(stderr, "[RTKLIB-C-PY-C] Expected ml_ar_hook.py:evaluate_ar, got %s:%s\n",
                path, (fn && *fn) ? fn : "evaluate_ar");
        return 0;
    }
    if (!(stream = fopen(path, "rb"))) {
        fprintf(stderr, "[RTKLIB-C-PY-C] Cannot open ML hook %s: %s\n", path, strerror(errno));
        return 0;
    }
    if (!Py_IsInitialized()) {
        Py_Initialize();
        g_owns_python = 1;
    }
    /* Execute this exact file; a failed path must not import a namesake on sys.path. */
    g_pModule = PyModule_New("ml_ar_hook");
    if (!g_pModule) goto failed;
    globals = PyModule_GetDict(g_pModule);
    value = PyUnicode_DecodeFSDefault(path);
    if (!value || PyDict_SetItemString(globals, "__file__", value) < 0) goto failed;
    Py_CLEAR(value);
    value = PyRun_FileEx(stream, path, Py_file_input, globals, globals, 1);
    stream = NULL; /* PyRun_FileEx closed it. */
    if (!value) goto failed;
    Py_CLEAR(value);

    g_pFunc = PyObject_GetAttrString(g_pModule, "evaluate_ar");
    if (!g_pFunc) goto failed;
    if (!PyCallable_Check(g_pFunc)) {
        PyErr_SetString(PyExc_TypeError, "evaluate_ar must be callable");
        goto failed;
    }
    init = PyObject_GetAttrString(g_pModule, "initialize_model");
    if (!init) goto failed;
    value = PyObject_CallObject(init, NULL);
    if (!value) goto failed;
    if (value != Py_True) {
        PyErr_SetString(PyExc_RuntimeError, "initialize_model did not confirm readiness");
        goto failed;
    }
    Py_DECREF(value);
    Py_DECREF(init);
    g_py_active = 1;
    fprintf(stderr, "[RTKLIB-C-PY-C] ML ready: %s:evaluate_ar\n", path);
    return 1;

failed:
    if (stream) fclose(stream);
    fprintf(stderr, "[RTKLIB-C-PY-C] ML startup failed: %s\n", path);
    if (PyErr_Occurred()) PyErr_Print();
    Py_XDECREF(value);
    Py_XDECREF(init);
    py_ar_cleanup();
    return 0;
}

int py_ar_is_active(void)
{
    return g_py_active;
}

int py_ar_evaluate(const double *features, int n_feat)
{
    PyObject *result;
    long decision;
    if (!g_py_active || !features || n_feat != 6) return 0;
    result = PyObject_CallFunction(g_pFunc, "dddddd", features[0], features[1],
                                   features[2], features[3], features[4], features[5]);
    if (result && PyLong_CheckExact(result)) {
        decision = PyLong_AsLong(result);
        if (!PyErr_Occurred() && (decision == 0 || decision == 1)) {
            Py_DECREF(result);
            return (int)decision;
        }
    }
    fprintf(stderr, "[RTKLIB-C-PY-C] evaluate_ar failed or did not return integer 0 or 1; "
                    "rejecting, ratio=%.17g lasting_epochs=%.17g blen_km=%.17g "
                    "amb_dim=%.17g pdop=%.17g vtec=%.17g\n",
            features[0], features[1], features[2], features[3], features[4], features[5]);
    if (PyErr_Occurred()) PyErr_Print();
    Py_XDECREF(result);
    return 0;
}

void py_ar_cleanup(void)
{
    g_py_active = 0;
    Py_CLEAR(g_pFunc);
    Py_CLEAR(g_pModule);
    if (g_owns_python) {
        g_owns_python = 0;
        Py_Finalize();
    }
}
