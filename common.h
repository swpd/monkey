#include "duda_api.h"

#define MARIADB_OK 0
#define MARIADB_ERR -1

#define FREE(p) if (p) { monkey->mem_free(p); p = NULL; }
