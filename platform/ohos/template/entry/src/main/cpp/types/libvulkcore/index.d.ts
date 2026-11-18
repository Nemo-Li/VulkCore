import { resourceManager } from '@kit.LocalizationKit';
/** Queues extraction in the background. Read failures through getError(). */
export const prepare: (resources: resourceManager.ResourceManager, filesDir: string) => void;
/** 0–74: extraction, 75: assets ready, 88: loading scene, 97: waiting for frame, 100: frame presented. */
export const getLoadingProgress: () => number;
export const setKey: (key: number, pressed: boolean) => void;
export const setMovement: (right: number, forward: number) => void;
export const setPaused: (paused: boolean) => void;
export const getError: () => string;
