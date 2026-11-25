// The one place the page talks to Rust. Every invoke() goes through here, next
// to the TypeScript shape of what comes back - the same structures as on the
// Rust side, with camelCase fields.

import { invoke } from "@tauri-apps/api/core";

/** Mirrors `AppError` in src-tauri/src/error.rs. */
export type AppErrorKind = "io" | "json" | "font" | "pdf" | "validation";

export interface AppError {
  kind: AppErrorKind;
  message: string;
}

export function isAppError(value: unknown): value is AppError {
  return (
    typeof value === "object" &&
    value !== null &&
    typeof (value as AppError).kind === "string" &&
    typeof (value as AppError).message === "string"
  );
}

/** Mirrors `AppInfo` in src-tauri/src/commands.rs. */
export interface AppInfo {
  product: string;
  version: string;
  displayVersion: string;
  copyright: string;
}

export function appInfo(): Promise<AppInfo> {
  return invoke<AppInfo>("app_info");
}
