// The one place the page talks to Rust. Every invoke() goes through here, next
// to the TypeScript shape of what comes back - the same structures as on the
// Rust side, with camelCase fields. When a Rust struct changes, this file
// changes in the same commit.

import { invoke } from "@tauri-apps/api/core";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";

// ---------------------------------------------------------------- errors

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

/** The text of whatever a command threw: an AppError's message, or the string
 *  Tauri itself produces when arguments do not match. */
export function errorText(error: unknown): string {
  if (isAppError(error)) return error.message;
  return typeof error === "string" ? error : String(error);
}

// ----------------------------------------------------------------- model

/** Mirrors `Theme` in crates/cvb-core/src/model.rs. */
export interface Theme {
  background: string;
  rule: string;
  heading: string;
  body: string;
  subtle: string;
  faint: string;
  accent: string;
  accent2: string;
}

export type ThemeRole = keyof Theme;

/** In the order the editor lists them; mirrors `ThemeRole::ALL`. */
export const THEME_ROLES: readonly ThemeRole[] = [
  "background",
  "rule",
  "heading",
  "body",
  "subtle",
  "faint",
  "accent",
  "accent2",
];

export interface Job {
  title: string;
  period: string;
  company: string;
  location: string;
  bullets: string[];
}

export interface Skill {
  name: string;
  highlight: boolean;
}

export interface SkillGroup {
  title: string;
  skills: Skill[];
}

export interface Education {
  title: string;
  subtitle: string;
  highlight: boolean;
}

/** Mirrors `SECTION_IDS`. */
export type SectionId =
  | "summary"
  | "studies"
  | "jobs"
  | "skill_groups"
  | "soft_skills"
  | "education"
  | "lab"
  | "volunteering";

export interface SectionRef {
  id: SectionId;
  label: string;
  enabled: boolean;
  order: number;
}

/** Mirrors `Cv`. */
export interface Cv {
  name: string;
  role: string;
  email: string;
  location: string;
  website: string;
  summary: string;
  jobs: Job[];
  volunteering: Job[];
  skillGroups: SkillGroup[];
  softSkills: string[];
  education: Education[];
  studies: Job[];
  labBullets: string[];
  sections: SectionRef[];
  theme: Theme;
}

export interface Preset {
  name: string;
  theme: Theme;
}

export interface Defaults {
  cv: Cv;
  newDocument: Cv;
}

// --------------------------------------------------------------- preview

/** Mirrors src-tauri/src/preview.rs. */
export interface PreviewLine {
  x1: number;
  y1: number;
  x2: number;
  y2: number;
  width: number;
  color: string;
}

export interface PreviewRun {
  y: number;
  size: number;
  bold: boolean;
  color: string;
  text: string;
  /** One x per UTF-16 code unit of `text`. */
  xs: number[];
}

export interface PreviewPage {
  background: string;
  lines: PreviewLine[];
  runs: PreviewRun[];
}

export interface PreviewDoc {
  width: number;
  height: number;
  pages: PreviewPage[];
}

// ------------------------------------------------------------- documents

/** Mirrors `Opened` in src-tauri/src/document.rs. */
export interface Opened {
  cv: Cv;
  status: string;
  dirty: boolean;
}

export type ThemeMode = "system" | "light" | "dark";

/** Mirrors `AppInfo` in src-tauri/src/commands.rs. */
export interface AppInfo {
  product: string;
  version: string;
  displayVersion: string;
  copyright: string;
}

// -------------------------------------------------------------- commands

export const appInfo = (): Promise<AppInfo> => invoke("app_info");
export const defaults = (): Promise<Defaults> => invoke("defaults");
export const presets = (): Promise<Preset[]> => invoke("presets");

export const startup = (): Promise<Opened> => invoke("startup");
export const setDirty = (dirty: boolean): Promise<void> => invoke("set_dirty", { dirty });

// Each of these may ask about unsaved changes first; null means the user
// cancelled somewhere along the way.
export const newDocument = (cv: Cv): Promise<Opened | null> => invoke("new_document", { cv });
export const openDialog = (cv: Cv): Promise<Opened | null> => invoke("open_dialog", { cv });
export const openRecent = (index: number, cv: Cv): Promise<Opened | null> =>
  invoke("open_recent", { index, cv });
export const openPending = (cv: Cv): Promise<Opened | null> => invoke("open_pending", { cv });

/** The status-line text, or null when no file was chosen. */
export const save = (cv: Cv): Promise<string | null> => invoke("save", { cv });
export const saveAs = (cv: Cv): Promise<string | null> => invoke("save_as", { cv });
export const exportPdf = (cv: Cv): Promise<string | null> => invoke("export_pdf", { cv });

/** False when the user chose to stay. */
export const closeWindow = (cv: Cv): Promise<boolean> => invoke("close_window", { cv });

export const writeRecovery = (cv: Cv): Promise<void> => invoke("write_recovery", { cv });
export const render = (cv: Cv): Promise<PreviewDoc> => invoke("render", { cv });

export const recentFiles = (): Promise<string[]> => invoke("recent_files");
export const clearRecent = (): Promise<void> => invoke("clear_recent");

export const themeMode = (): Promise<ThemeMode> => invoke("theme_mode");
export const setThemeMode = (mode: ThemeMode): Promise<void> => invoke("set_theme_mode", { mode });

// ---------------------------------------------------------------- events

/** Files were dropped on the window or handed over by a second launch. */
export function onPendingFiles(handler: (count: number) => void): Promise<UnlistenFn> {
  return listen<{ count: number }>("open-pending", (event) => handler(event.payload.count));
}

/** The window is being closed with unsaved changes. */
export function onCloseRequested(handler: () => void): Promise<UnlistenFn> {
  return listen("close-requested", () => handler());
}
