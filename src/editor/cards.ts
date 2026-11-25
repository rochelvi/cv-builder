// The contents of each kind of card: a job (also used for volunteering and
// detailed studies), a skill group, an education entry.

import type { Education, Job, SkillGroup } from "../api";
import { el, replaceChildren } from "../dom";
import { stringList } from "./lists";
import { addButton, checkbox, field, toolButton } from "./widgets";

export interface JobHints {
  title: string;
  period: string;
  company: string;
  location: string;
  bullet: string;
}

export const JOB_HINTS: JobHints = {
  title: "Должность",
  period: "2022 – 2025 · 3 года",
  company: "Компания",
  location: "Город",
  bullet: "Достижение или обязанность",
};

export const STUDY_HINTS: JobHints = {
  title: "Программа, специальность или степень",
  period: "2022 – 2026",
  company: "Университет или школа",
  location: "Город",
  bullet: "Курс, проект, олимпиада, активность",
};

export function jobCard(job: Job, hints: JobHints, changed: () => void): Node[] {
  const set = (key: "title" | "period" | "company" | "location") => (text: string) => {
    job[key] = text;
    changed();
  };
  return [
    el("div", { className: "pair" }, field(job.title, hints.title, set("title")), field(job.period, hints.period, set("period"))),
    el(
      "div",
      { className: "pair" },
      field(job.company, hints.company, set("company")),
      field(job.location, hints.location, set("location")),
    ),
    el("div", { className: "label" }, "Пункты"),
    stringList(job.bullets, hints.bullet, changed),
  ];
}

export function skillGroupCard(group: SkillGroup, changed: () => void): Node[] {
  const rows = el("div", { className: "rows" });
  const render = (): void => {
    replaceChildren(
      rows,
      ...group.skills.map((skill, index) =>
        el(
          "div",
          { className: "row" },
          field(skill.name, "Навык", (text) => {
            skill.name = text;
            changed();
          }),
          checkbox("выделить", skill.highlight, (on) => {
            skill.highlight = on;
            changed();
          }),
          el(
            "span",
            { className: "tools" },
            toolButton("✕", "Удалить", () => {
              group.skills.splice(index, 1);
              render();
              changed();
            }),
          ),
        ),
      ),
    );
  };
  render();
  return [
    field(group.title, "Название группы, например «Сети»", (text) => {
      group.title = text;
      changed();
    }),
    rows,
    addButton("+ Навык", () => {
      group.skills.push({ name: "", highlight: false });
      render();
      changed();
      const inputs = rows.querySelectorAll<HTMLInputElement>("input.field");
      inputs[inputs.length - 1]?.focus();
    }),
  ];
}

export function educationCard(item: Education, changed: () => void): Node[] {
  return [
    field(item.title, "Программа или сертификация", (text) => {
      item.title = text;
      changed();
    }),
    field(item.subtitle, "Учреждение, год / статус", (text) => {
      item.subtitle = text;
      changed();
    }),
    checkbox("выделить", item.highlight, (on) => {
      item.highlight = on;
      changed();
    }),
  ];
}
