/*
 * gridengine_compat
 *
 * SLURM SPANK plugin that implements some GridEngine compatibility
 * behaviors:
 *
 *   - Sets some GridEngine per-job environment variables using their SLURM equivalents
 *     if the user requested with a flag to sbatch/salloc/run:
 *
 *        SGE_O_WORKDIR           = SLURM_SUBMIT_DIR
 *        JOB_ID                  = SLURM_ARRAY_JOB_ID or SLURM_JOB_ID
 *        JOB_NAME                = SLURM_JOB_NAME
 *        NHOSTS                  = SLURM_JOB_NUM_NODES
 *        NSLOTS                  = SLURM_JOB_CPUS_PER_NODE (evaluated and summed)
 *        TASK_ID                 = SLURM_ARRAY_TASK_ID
 *        SGE_TASK_FIRST          = SLURM_ARRAY_TASK_MIN
 *        SGE_TASK_LAST           = SLURM_ARRAY_TASK_MAX
 *        SGE_TASK_STEPSIZE       = SLURM_ARRAY_TASK_STEP
 *
 *   - Changes the job's working directory to the directory at the time of job
 *     submission if the user requested with a flag to sbatch/salloc/srun
 *
 */

#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>

#include <slurm/spank.h>
#include <slurm/slurm.h>

/* 
 * All spank plugins must define this macro for the SLURM plugin loader.
 */
SPANK_PLUGIN(gridengine_compat, 1)

/*
 * Should the job environment be augmented by SGE_* versions of the SLURM
 * environment variables?
 */
static int should_add_sge_env = 0;

/*
 * @function _opt_add_sge_env
 *
 * Parse the --add-sge-env option.
 *
 */
static int _opt_add_sge_env(
  int         val,
  const char  *optarg,
  int         remote
)
{
  should_add_sge_env = 1;
  slurm_verbose("gridengine_compat:  will add SGE-style environment variables to job");
  return ESPANK_SUCCESS;
}

/*
 * Options available to this spank plugin:
 */
struct spank_option spank_options[] =
{
    { "add-sge-env", NULL,
      "Add GridEngine equivalents of SLURM job environment variables.",
      0, 0, (spank_opt_cb_f) _opt_add_sge_env },
      
    SPANK_OPTIONS_TABLE_END
};

/*
 * @function slurm_spank_init
 *
 * In the ALLOCATOR context, the 'spank_options' don't get automatically
 * registered as they do under LOCAL and REMOTE.  So under that context
 * we explicitly register our cli options.
 *
 */
int
slurm_spank_init(
  spank_t       spank_ctxt,
  int           argc,
  char          *argv[]
)
{
  int                     rc = ESPANK_SUCCESS;
  int                     i;
  
  if ( spank_context() == S_CTX_ALLOCATOR ) {
    struct spank_option   *o = spank_options;
    
    while ( o->name && (rc == ESPANK_SUCCESS) ) rc = spank_option_register(spank_ctxt, o++);
  }
  
  for ( i = 0; i < argc; i++ ) {
    if ( strncmp("enable=", argv[i], 7) == 0 ) {
      const char          *optarg = argv[i] + 7;
      
      if ( isdigit(*optarg) ) {
        char              *e;
        long              v = strtol(optarg, &e, 10);
        
        if ( e > optarg && ! *e ) {
          if ( v ) should_add_sge_env = 1;
        } else {
          slurm_error("gridengine_compat: Ignoring invalid enable option: %s", argv[i]);
        }
      } else if ( ! strcasecmp(optarg, "y") || ! strcasecmp(optarg, "yes") || ! strcasecmp(optarg, "t") || ! strcasecmp(optarg, "true") ) {
        should_add_sge_env = 1;
      } else if ( strcasecmp(optarg, "n") && strcasecmp(optarg, "no") && strcasecmp(optarg, "f") && strcasecmp(optarg, "false") ) {
        slurm_error("gridengine_compat: Ignoring invalid enable option: %s", argv[i]);
      }
    } else {
      slurm_error("gridengine_compat: Invalid option: %s", argv[i]);
    }
  }
  
  return rc;
}


/*
 * @function slurm_spank_task_init
 *
 * Set GridEngine versions of environment variables.
 *
 * (Called as job user after fork() and before execve().)
 */
int
slurm_spank_task_init(
  spank_t       spank_ctxt,
  int           argc,
  char*         *argv
)
{
  char          value[8192];
  int           did_set_nslots = 0;
  
  if ( spank_remote(spank_ctxt) && should_add_sge_env ) {
    
    if ( spank_getenv(spank_ctxt, "SLURM_CLUSTER_NAME", value, sizeof(value)) == ESPANK_SUCCESS && *value ) spank_setenv(spank_ctxt, "SGE_CLUSTER_NAME", value, 1);
    
    if ( spank_getenv(spank_ctxt, "SLURM_SUBMIT_DIR", value, sizeof(value)) == ESPANK_SUCCESS && *value ) spank_setenv(spank_ctxt, "SGE_O_WORKDIR", value, 1);
    
    if ( spank_getenv(spank_ctxt, "SLURM_SUBMIT_HOST", value, sizeof(value)) == ESPANK_SUCCESS && *value ) spank_setenv(spank_ctxt, "SGE_O_HOST", value, 1);
    
    if ( spank_getenv(spank_ctxt, "SLURM_ARRAY_JOB_ID", value, sizeof(value)) == ESPANK_SUCCESS && *value ) {
      spank_setenv(spank_ctxt, "JOB_ID", value, 1);
    
      if ( spank_getenv(spank_ctxt, "SLURM_ARRAY_TASK_ID", value, sizeof(value)) == ESPANK_SUCCESS && *value ) spank_setenv(spank_ctxt, "SGE_TASK_ID", value, 1);
      
      if ( spank_getenv(spank_ctxt, "SLURM_ARRAY_TASK_MIN", value, sizeof(value)) == ESPANK_SUCCESS && *value ) spank_setenv(spank_ctxt, "SGE_TASK_FIRST", value, 1);
      
      if ( spank_getenv(spank_ctxt, "SLURM_ARRAY_TASK_MAX", value, sizeof(value)) == ESPANK_SUCCESS && *value ) spank_setenv(spank_ctxt, "SGE_TASK_LAST", value, 1);
      
      if ( spank_getenv(spank_ctxt, "SLURM_ARRAY_TASK_STEP", value, sizeof(value)) == ESPANK_SUCCESS && *value ) spank_setenv(spank_ctxt, "SGE_TASK_STEPSIZE", value, 1);
    }
    else if ( spank_getenv(spank_ctxt, "SLURM_JOB_ID", value, sizeof(value)) == ESPANK_SUCCESS && *value ) {
      spank_setenv(spank_ctxt, "JOB_ID", value, 1);
    }
    
    if ( spank_getenv(spank_ctxt, "SLURM_JOB_NAME", value, sizeof(value)) == ESPANK_SUCCESS && *value ) {
      spank_setenv(spank_ctxt, "JOB_NAME", value, 1);
    }
    
    if ( spank_getenv(spank_ctxt, "SLURM_JOB_PARTITION", value, sizeof(value)) == ESPANK_SUCCESS && *value ) spank_setenv(spank_ctxt, "QUEUE", value, 1);
    spank_setenv(spank_ctxt, "NQUEUES", "1", 1);
    
    if ( spank_getenv(spank_ctxt, "SLURM_JOB_NUM_NODES", value, sizeof(value)) == ESPANK_SUCCESS && *value ) {
      spank_setenv(spank_ctxt, "NHOSTS", value, 1);
    } else {
      spank_setenv(spank_ctxt, "NHOSTS", "1", 1);
    }
    
    /*
     * We will not setup a PE_HOSTFILE, since doing so could cause tightly-integrated
     * MPI implementations (like Open MPI) to mistakenly think that we're actually
     * using Grid Engine.
     */
    
    /*
     * Hmm...how do we calculate NSLOTS for a job step?  Looks like the
     * best bet is SLURM_JOB_CPUS_PER_NODE, which is a comma-delimited list
     * of integers with optional repeat counts:
     *
     *    1(x2),2(x3) == 1,1,2,2,2
     *
     * the sequence matches that of node names that are presented in the
     * SLURM_JOB_NODELIST variable (as a set of SLURM hostname expressions).
     *
     */
    if ( (spank_getenv(spank_ctxt, "SLURM_JOB_CPUS_PER_NODE", value, sizeof(value)) == ESPANK_SUCCESS) && *value ) {
      unsigned  nslots = 0;
      char      *p = value;
      
      while ( *p ) {
        char    *e;
        long    n;
        
        if ( ((n = strtol(p, &e, 10)) > 0) && (e > p) ) {
          if ( e[0] == '(' && e[1] == 'x' ) {
            long    r;
            
            p = e + 2;
            if ( ((r = strtol(p, &e, 10)) > 0) && (e > p) ) {
              n *= r;
              p = ( e[0] == ')' ) ? (e + 1) : e;
            } else {
              slurm_error("gridengine_compat: slurm_spank_task_init: Unable to parse SLURM_JOB_CPUS_PER_NODE (at index %ld): %s", (p - value), value);
              break;
            }
          } else {
            p = e;
          }
          nslots += n;
          if ( *p ) {
            if ( *p == ',' ) {
              p++;
            } else {
              slurm_error("gridengine_compat: slurm_spank_task_init: Unable to parse SLURM_JOB_CPUS_PER_NODE (at index %ld): %s", (p - value), value);
              break;
            }
          }
        } else {
          slurm_error("gridengine_compat: slurm_spank_task_init: Unable to parse SLURM_JOB_CPUS_PER_NODE (at index %ld): %s", (p - value), value);
          break;
        }
      }
      if ( (nslots > 0) && (snprintf(value, sizeof(value), "%u", nslots) > 0) ) {
        spank_setenv(spank_ctxt, "NSLOTS", value, 1);
        did_set_nslots = 1;
      }
    }
    if ( ! did_set_nslots ) {
      spank_setenv(spank_ctxt, "NSLOTS", "1", 1);
    }
  }
  return (0);
}
