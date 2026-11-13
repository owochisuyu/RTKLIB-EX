#ifndef PY_AR_HOOK_H
#define PY_AR_HOOK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize Python AR Hook
 * If script_path is NULL or empty, checks getenv("RTK_PY_AR_HOOK")
 * Returns 1 if active and successfully loaded, 0 otherwise
 */
int py_ar_init(const char *script_path, const char *func_name);

/* Check if Python hook is currently active */
int py_ar_is_active(void);

/* Evaluate ambiguity acceptance via Python
 * features: [0]=ratio, [1]=lasting_epochs, [2]=blen_km, [3]=amb_dim, [4]=pdop, [5]=vtec
 * Returns 1 (accepted/fixed), 0 (rejected/float)
 */
int py_ar_evaluate(const double *features, int n_feat);

/* Finalize embedded Python interpreter */
void py_ar_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* PY_AR_HOOK_H */
