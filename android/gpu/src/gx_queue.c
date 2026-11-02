#include "gx_queue.h"

static gxCmdQueue_s *boundQueue;

/* Keep this definition separate from core.c: upstream uses the linker's
 * --wrap=GX_BindQueue hook to observe Citro3D's public queue pointer. */
void GX_BindQueue(gxCmdQueue_s *queue)
{
    boundQueue = queue;
}

void gpuGxRecordCommand(void)
{
    /* Commands execute through GLES rather than filling a PICA command array.
     * Saturation prevents wraparound from inventing free upload capacity. */
    if (boundQueue && boundQueue->numEntries < boundQueue->maxEntries)
        ++boundQueue->numEntries;
}

/* GLES commands are submitted immediately, so no queued PICA work remains. */
void gxCmdQueueRun(gxCmdQueue_s *queue)
{
    if(queue) queue->curEntry=queue->lastEntry=queue->numEntries;
}
void gxCmdQueueStop(gxCmdQueue_s *queue)
{
    if(queue) queue->curEntry=queue->lastEntry=queue->numEntries;
}
