// Encode/decode the 40-byte snapshot defined in the C ABI
// (game/src/SR cpp/network/sr_api.cpp). The byte layout is mirrored on
// the C++ side; bumping it requires bumping PROTOCOL_VERSION.

import { SNAPSHOT_BYTES, SNAPSHOT_OFFSETS } from "@sr-web/protocol";

export interface DecodedSnapshot {
	posX: number;
	posY: number;
	velX: number;
	velY: number;
	facing: number;
	anim: number;
	grappleActive: boolean;
	grappleTaut: boolean;
	grappleOriginX: number;
	grappleOriginY: number;
	grappleAttachX: number;
	grappleAttachY: number;
	grappleLength: number;
	sizeX: number;
	sizeY: number;
}

export function bytesToBase64(bytes: Uint8Array): string {
	let s = "";
	for (let i = 0; i < bytes.length; i++) s += String.fromCharCode(bytes[i] ?? 0);
	return btoa(s);
}

export function base64ToBytes(b64: string): Uint8Array {
	const s = atob(b64);
	const out = new Uint8Array(s.length);
	for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i);
	return out;
}

export function decodeSnapshot(bytes: Uint8Array): DecodedSnapshot | null {
	if (bytes.byteLength < SNAPSHOT_BYTES) return null;
	const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
	return {
		posX: dv.getFloat32(SNAPSHOT_OFFSETS.posX, true),
		posY: dv.getFloat32(SNAPSHOT_OFFSETS.posY, true),
		velX: dv.getFloat32(SNAPSHOT_OFFSETS.velX, true),
		velY: dv.getFloat32(SNAPSHOT_OFFSETS.velY, true),
		facing: dv.getInt8(SNAPSHOT_OFFSETS.facing),
		anim: dv.getUint8(SNAPSHOT_OFFSETS.anim),
		grappleActive: dv.getUint8(SNAPSHOT_OFFSETS.grappleActive) !== 0,
		grappleTaut: dv.getUint8(SNAPSHOT_OFFSETS.grappleTaut) !== 0,
		grappleOriginX: dv.getFloat32(SNAPSHOT_OFFSETS.grappleOriginX, true),
		grappleOriginY: dv.getFloat32(SNAPSHOT_OFFSETS.grappleOriginY, true),
		grappleAttachX: dv.getFloat32(SNAPSHOT_OFFSETS.grappleAttachX, true),
		grappleAttachY: dv.getFloat32(SNAPSHOT_OFFSETS.grappleAttachY, true),
		grappleLength: dv.getFloat32(SNAPSHOT_OFFSETS.grappleLength, true),
		sizeX: dv.getFloat32(SNAPSHOT_OFFSETS.sizeX, true),
		sizeY: dv.getFloat32(SNAPSHOT_OFFSETS.sizeY, true),
	};
}
