// The left pane: the whole resume as a form, in the order the earlier
// versions had it - colours, sections, header, summary, then each block.
//
// The editor works on its own copy of the resume. Every edit changes that copy
// and reports it; `collect()` hands out a copy for Rust, with empty section
// headings filled in the way the earlier editors filled them.

import type { Cv, Defaults, Education, Job, Preset, SkillGroup } from "../api";
import { el, replaceChildren } from "../dom";
import { JOB_HINTS, STUDY_HINTS, educationCard, jobCard, skillGroupCard } from "./cards";
import { colorsBlock } from "./colors";
import { cardList, stringList } from "./lists";
import { sectionsBlock } from "./sections";
import { area, field, heading, hint } from "./widgets";

export class Editor {
  readonly element: HTMLElement;
  private cv: Cv;
  private readonly defaultLabels: Map<string, string>;

  constructor(
    defaults: Defaults,
    private readonly presets: Preset[],
    private readonly onChange: () => void,
  ) {
    this.element = el("div", { className: "editor" });
    this.cv = structuredClone(defaults.cv);
    this.defaultLabels = new Map(defaults.cv.sections.map((s) => [s.id, s.label]));
    this.build();
  }

  /** Shows a whole resume. Replaces the form rather than patching it, so no
   *  state of the previous document survives into the next. */
  setCv(cv: Cv): void {
    this.cv = structuredClone(cv);
    const scroll = this.element.scrollTop;
    this.build();
    this.element.scrollTop = scroll;
  }

  collect(): Cv {
    const cv = structuredClone(this.cv);
    cv.sections.forEach((section, index) => {
      if (section.label === "") section.label = this.defaultLabels.get(section.id) ?? "";
      // Written from the row's position, so the list on screen is the order in
      // the file.
      section.order = index;
    });
    return cv;
  }

  private build(): void {
    const cv = this.cv;
    const changed = this.onChange;
    const set = (key: "name" | "role" | "email" | "location" | "website" | "summary") => (text: string) => {
      cv[key] = text;
      changed();
    };

    const block = (title: string, body: Node, note?: string): HTMLElement =>
      el("section", { className: "block" }, heading(title), note ? hint(note) : null, body);

    replaceChildren(
      this.element,
      block("Цвета", colorsBlock(cv.theme, this.presets, changed)),
      block(
        "Разделы",
        sectionsBlock(cv.sections, changed),
        "Снимите галочку, чтобы убрать раздел со страницы; ↑ ↓ меняют порядок.",
      ),
      block(
        "Шапка",
        el(
          "div",
          { className: "stack" },
          field(cv.name, "Полное имя", set("name")),
          field(cv.role, "Должность, например SYSTEM ADMINISTRATOR", set("role")),
          el(
            "div",
            { className: "triple" },
            field(cv.email, "Email", set("email")),
            field(cv.location, "Город, страна", set("location")),
            field(cv.website, "Сайт / GitHub", set("website")),
          ),
        ),
      ),
      block("О себе", area(cv.summary, set("summary"))),
      block(
        "Опыт работы",
        cardList<Job>(
          cv.jobs,
          "+ Добавить работу",
          () => ({ title: "Должность", period: "2024 – 2025", company: "Компания", location: "", bullets: [] }),
          (job, ch) => jobCard(job, JOB_HINTS, ch),
          changed,
        ),
      ),
      block(
        "Волонтёрство",
        cardList<Job>(
          cv.volunteering,
          "+ Добавить волонтёрство",
          () => ({ title: "Роль", period: "2024", company: "Организация", location: "", bullets: [] }),
          (job, ch) => jobCard(job, JOB_HINTS, ch),
          changed,
        ),
      ),
      block(
        "Технические навыки",
        cardList<SkillGroup>(
          cv.skillGroups,
          "+ Добавить группу навыков",
          () => ({ title: "Новая группа", skills: [{ name: "Навык", highlight: true }] }),
          skillGroupCard,
          changed,
        ),
        "Группы заполняют две колонки слева направо.",
      ),
      block("Soft skills", stringList(cv.softSkills, "Soft skill", changed)),
      block(
        "Учёба",
        cardList<Job>(
          cv.studies,
          "+ Добавить учёбу",
          () => ({ title: "Программа обучения", period: "2022 – 2026", company: "Университет", location: "", bullets: [] }),
          (job, ch) => jobCard(job, STUDY_HINTS, ch),
          changed,
        ),
        "Развёрнутая учёба: программа, вуз, годы и чем занимались — как в опыте работы.",
      ),
      block(
        "Сертификаты и курсы",
        cardList<Education>(
          cv.education,
          "+ Добавить запись",
          () => ({ title: "Сертификация", subtitle: "Запланировано", highlight: false }),
          educationCard,
          changed,
        ),
      ),
      block("Личная лаборатория", stringList(cv.labBullets, "Проект или пункт лаборатории", changed)),
    );
  }
}
