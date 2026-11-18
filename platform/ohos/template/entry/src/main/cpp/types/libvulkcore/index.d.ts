import { resourceManager } from '@kit.LocalizationKit';
export const prepare: (resources: resourceManager.ResourceManager, filesDir: string) => void;
export const setKey: (key: number, pressed: boolean) => void;
export const setPaused: (paused: boolean) => void;
export const getError: () => string;
