/* Host-side entry points for packaging with the NRO's DLL manager and feature table. */
#include "horizon_dlls.h"
#include "horizon_dll_features.h"

#include <stdio.h>
#include <string.h>

#define FEATURE_COUNT (sizeof(horizon_dll_runtime_features) / sizeof(horizon_dll_runtime_features[0]))

static int check_result(enum horizon_dlls_result result)
{
    if (result == HORIZON_DLLS_OK) return 1;
    fprintf(stderr, "%s\n", horizon_dlls_error(result));
    return 0;
}

int main(int argc, char **argv)
{
    struct horizon_dll_manifest remote;
    struct horizon_dlls_plan plan;
    char why[256];

    if (argc == 2 && !strcmp(argv[1], "revision"))
    {
        puts(HORIZON_DLLS_REVISION);
        return 0;
    }
    if (argc < 3 || !check_result(horizon_dlls_load(argv[2], horizon_dll_runtime_features, FEATURE_COUNT, &remote)))
        return 1;
    if (remote.schema < 2 || strcmp(remote.flavor, HORIZON_DLLS_FLAVOR))
    {
        fprintf(stderr, "Unsupported DLL manifest schema or flavor\n");
        return 1;
    }
    if (argc == 3 && !strcmp(argv[1], "list"))
    {
        for (unsigned int i = 0; i < remote.count; i++)
        {
            const struct horizon_dll_file *file = &remote.files[i];
            if (file->satisfied) printf("%s/%s\n", horizon_dlls_folders[file->folder], file->name);
        }
        return 0;
    }
    if (argc != 4 || (strcmp(argv[1], "record") && strcmp(argv[1], "verify"))) return 1;
    const char *card = argv[3];
    if (!check_result(horizon_dlls_plan(card, &remote, NULL, 1, NULL, NULL, &plan))) return 1;
    printf("%s: current=%u pending=%u unsupported=%u skipped=%u\n",
           argv[1], plan.current, plan.pending, plan.unsupported, remote.skipped);
    if (!plan.current || plan.pending) return 1;

    if (!strcmp(argv[1], "record"))
    {
        /* All supported files have just passed their SHA-256 checks. Apply only writes the local records. */
        struct horizon_dlls_transport unused = {0};
        if (!check_result(horizon_dlls_apply(card, &remote, NULL, &unused, 1, NULL, NULL))) return 1;
    }
    if (!horizon_dlls_ready(card, horizon_dll_runtime_features, FEATURE_COUNT, why, sizeof(why)))
    {
        fprintf(stderr, "%s\n", why);
        return 1;
    }
    puts("ready=1");
    horizon_dlls_free(&remote);
    return 0;
}
