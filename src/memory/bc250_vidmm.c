#include "bc250_vidmm.h"

#define BC250_PAGING_BUFFER_SIZE (64u * 1024u)

NTSTATUS
Bc250QueryUmaSegments(
    _In_ const DXGK_QUERYSEGMENTIN* QueryIn,
    _Inout_ DXGK_QUERYSEGMENTOUT* QueryOut,
    _In_ SIZE_T OutputDataSize
    )
{
    DXGK_SEGMENTDESCRIPTOR* descriptor;
    SIZE_T descriptorBytes;

    if (QueryIn == NULL || QueryOut == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    if (QueryIn->AgpApertureSize.QuadPart <= 0) {
        return STATUS_DEVICE_NOT_READY;
    }

    descriptorBytes = sizeof(DXGK_SEGMENTDESCRIPTOR);
    if (QueryOut->pSegmentDescriptor == NULL ||
        OutputDataSize < descriptorBytes) {
        QueryOut->NbSegment = 1;
        QueryOut->PagingBufferSegmentId = 1;
        QueryOut->PagingBufferSize = BC250_PAGING_BUFFER_SIZE;
        QueryOut->PagingBufferPrivateDataSize = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    descriptor = QueryOut->pSegmentDescriptor;
    RtlZeroMemory(descriptor, descriptorBytes);

    /*
     * Segment 1 is the WDDM aperture. Segment 0 is the implicit system-memory
     * segment and is not returned by this callback. The address and flags are
     * supplied by dxgkrnl in DXGK_QUERYSEGMENTIN; the KMD does not guess a
     * physical base for BC-250's shared GDDR6.
     */
    descriptor->BaseAddress = QueryIn->AgpApertureBase;
    descriptor->CpuTranslatedAddress = QueryIn->AgpApertureBase;
    descriptor->Size = (SIZE_T)QueryIn->AgpApertureSize.QuadPart;
    descriptor->CommitLimit = descriptor->Size;
    descriptor->NbOfBanks = 0;
    descriptor->pBankRangeTable = NULL;
    descriptor->Flags = QueryIn->AgpFlags;

    QueryOut->NbSegment = 1;
    QueryOut->PagingBufferSegmentId = 1;
    QueryOut->PagingBufferSize = BC250_PAGING_BUFFER_SIZE;
    QueryOut->PagingBufferPrivateDataSize = 0;
    return STATUS_SUCCESS;
}
