// The words the editor puts on the model's own concepts: the eight colour
// roles, the eight sections, the theme modes. Not model data - the file stores
// an id, not a caption - so they live here, in one place.

import type { SectionId, ThemeMode, ThemeRole } from "./api";

export const THEME_ROLE_NAMES: Record<ThemeRole, string> = {
  background: "Фон страницы",
  rule: "Разделители",
  heading: "Заголовки (имя, должности)",
  body: "Основной текст",
  subtle: "Вторичный текст (компания, контакты)",
  faint: "Даты и неактивные записи",
  accent: "Акцент 1 (секции, маркеры)",
  accent2: "Акцент 2 (soft skills, образование)",
};

export const SECTION_NAMES: Record<SectionId, string> = {
  summary: "О себе",
  studies: "Учёба (подробно)",
  jobs: "Опыт работы",
  skill_groups: "Технические навыки",
  soft_skills: "Soft skills",
  education: "Сертификаты и курсы",
  lab: "Личная лаборатория",
  volunteering: "Волонтёрство",
};

export const THEME_MODES: readonly { mode: ThemeMode; name: string }[] = [
  { mode: "system", name: "Как в системе" },
  { mode: "light", name: "Светлая" },
  { mode: "dark", name: "Тёмная" },
];
