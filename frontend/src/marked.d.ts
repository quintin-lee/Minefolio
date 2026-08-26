// Type declarations for 'marked' module
declare module 'marked' {
  export function parse(src: string, options?: { async?: boolean; breaks?: boolean; gfm?: boolean }): string | Promise<string>;
  export function setOptions(options: Record<string, unknown>): void;
  export const marked: {
    parse: typeof parse;
    setOptions: typeof setOptions;
  };
}
