#ifndef PORT_REGION_DATA_H
#define PORT_REGION_DATA_H

/*
 * Resolve data whose address identifies a compiled USA-baseline blob.
 *
 * Data read from gRomData is already native to the active ROM and is returned
 * unchanged.  Known compiled blobs are replaced by their verified EU/JP ROM
 * counterpart.  A known blob which does not exist in the active region, or
 * whose replacement is unavailable, returns NULL so callers fail closed
 * instead of interpreting USA fields as regional flags/scripts.
 *
 * Unknown compiled pointers remain unchanged.  This deliberately avoids
 * guessing that arbitrary numeric fields are regional flag ordinals.
 */
const void* Port_ResolveRegionData(const void* data);

#endif /* PORT_REGION_DATA_H */
