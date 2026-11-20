#pragma once

#include "update_task.h"

/**
 * @brief Install the new resources, writing only what changed.
 *
 * Reads the Manifest inside the archive first, then removes only the files and folders the new
 * resources no longer have, and unpacks the rest, leaving alone every file that is already on
 * the card with the size and MD5 the new Manifest gives. /ext/Manifest is installed last, so it
 * never describes files that are not there yet.
 *
 * @return false when it could not start (nothing was changed) or stopped part way; the caller
 *         then does the full replacement
 */
bool update_task_resources_install(UpdateTask* update_task, const char* archive_path);

/**
 * @brief The next of the extra resource archives the manifest lists (see update.py).
 *
 * @param offset    where to go on in the list: 0 at first, then as left by the last call
 * @param path      set to the archive's full path
 * @return false when there are no more
 */
bool update_task_resources_next_extra(UpdateTask* update_task, size_t* offset, FuriString* path);

/** Whether a resource the old firmware installed stays when the new one has no such file */
bool update_task_resources_is_kept(const char* name);
