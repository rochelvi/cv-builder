"""Data model for the CV and JSON (de)serialisation."""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any


@dataclass
class Theme:
    """Every colour used by the renderer. All values are #rrggbb strings."""

    background: str = "#0d0f12"
    rule: str = "#2a2f3a"
    heading: str = "#e8ecf2"  # name, job titles, skill group titles
    body: str = "#9aa0ae"  # summary and bullet text
    subtle: str = "#7a8292"  # contacts, company lines, subtitles
    faint: str = "#4a5260"  # dates, non-highlighted certifications
    accent: str = "#4ade80"  # section titles, bullets, highlighted skills
    accent2: str = "#7ab8f5"  # soft skills, highlighted education

    def to_dict(self) -> dict[str, str]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: dict[str, Any] | None) -> "Theme":
        theme = cls()
        for key, value in (data or {}).items():
            if key in theme.__dict__ and isinstance(value, str):
                setattr(theme, key, value)
        return theme


THEME_LABELS: dict[str, str] = {
    "background": "Page background",
    "rule": "Separator lines",
    "heading": "Headings (name, job titles)",
    "body": "Body text",
    "subtle": "Secondary text (company, contacts)",
    "faint": "Dates & inactive entries",
    "accent": "Accent 1 (sections, bullets)",
    "accent2": "Accent 2 (soft skills, education)",
}

PRESETS: dict[str, Theme] = {
    "Dark (original)": Theme(),
    "Midnight blue": Theme(
        background="#0b1020", rule="#22304a", heading="#eaf1ff", body="#a8b6cf",
        subtle="#8593ad", faint="#4d5c78", accent="#5cc8ff", accent2="#b39bff",
    ),
    "Graphite orange": Theme(
        background="#141414", rule="#333333", heading="#f2f2f2", body="#b0b0b0",
        subtle="#8c8c8c", faint="#5a5a5a", accent="#ff8c42", accent2="#ffd166",
    ),
    "Paper (light)": Theme(
        background="#ffffff", rule="#d9dee5", heading="#111827", body="#374151",
        subtle="#6b7280", faint="#9ca3af", accent="#0f766e", accent2="#1d4ed8",
    ),
    "Warm cream": Theme(
        background="#f7f3ea", rule="#ded5c3", heading="#2b2113", body="#4d4335",
        subtle="#6f6553", faint="#9c9382", accent="#b4530a", accent2="#3c6e47",
    ),
}


@dataclass
class Job:
    title: str = ""
    period: str = ""
    company: str = ""
    location: str = ""
    bullets: list[str] = field(default_factory=list)


@dataclass
class Skill:
    name: str = ""
    highlight: bool = False


@dataclass
class SkillGroup:
    title: str = ""
    skills: list[Skill] = field(default_factory=list)


@dataclass
class Education:
    title: str = ""
    subtitle: str = ""
    highlight: bool = False


def _job(data: dict[str, Any]) -> "Job":
    return Job(
        title=data.get("title", ""),
        period=data.get("period", ""),
        company=data.get("company", ""),
        location=data.get("location", ""),
        bullets=list(data.get("bullets", [])),
    )


@dataclass
class CV:
    name: str = ""
    role: str = ""
    email: str = ""
    location: str = ""
    website: str = ""
    summary: str = ""
    jobs: list[Job] = field(default_factory=list)
    volunteer_title: str = "VOLUNTEER WORK"
    volunteering: list[Job] = field(default_factory=list)
    skill_groups: list[SkillGroup] = field(default_factory=list)
    soft_skills: list[str] = field(default_factory=list)
    education: list[Education] = field(default_factory=list)
    lab_title: str = "PERSONAL LAB"
    lab_bullets: list[str] = field(default_factory=list)
    theme: Theme = field(default_factory=Theme)

    # ---------- serialisation ----------
    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    def save(self, path: str | Path) -> None:
        Path(path).write_text(
            json.dumps(self.to_dict(), indent=2, ensure_ascii=False), encoding="utf-8"
        )

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "CV":
        return cls(
            name=data.get("name", ""),
            role=data.get("role", ""),
            email=data.get("email", ""),
            location=data.get("location", ""),
            website=data.get("website", ""),
            summary=data.get("summary", ""),
            jobs=[_job(j) for j in data.get("jobs", [])],
            volunteer_title=data.get("volunteer_title", "VOLUNTEER WORK"),
            volunteering=[_job(j) for j in data.get("volunteering", [])],
            skill_groups=[
                SkillGroup(
                    title=g.get("title", ""),
                    skills=[
                        Skill(name=s.get("name", ""), highlight=bool(s.get("highlight")))
                        for s in g.get("skills", [])
                    ],
                )
                for g in data.get("skill_groups", [])
            ],
            soft_skills=list(data.get("soft_skills", [])),
            education=[
                Education(
                    title=e.get("title", ""),
                    subtitle=e.get("subtitle", ""),
                    highlight=bool(e.get("highlight")),
                )
                for e in data.get("education", [])
            ],
            lab_title=data.get("lab_title", "PERSONAL LAB"),
            lab_bullets=list(data.get("lab_bullets", [])),
            theme=Theme.from_dict(data.get("theme")),
        )

    @classmethod
    def load(cls, path: str | Path) -> "CV":
        return cls.from_dict(json.loads(Path(path).read_text(encoding="utf-8")))


def sample_cv() -> CV:
    """The CV that ships with the app, matching the original template."""
    return CV(
        name="Daniil Mishin",
        role="SYSTEM ADMINISTRATOR",
        email="mishindaniil365247@gmail.com",
        location="Tashkent, Uzbekistan",
        website="github.com/rochelvi",
        summary=(
            "System administrator with 3+ years of hands-on experience managing Windows Server "
            "and Linux environments, Active Directory, network infrastructure, and virtualization. "
            "Graduate of School 21 Tashkent cybersecurity program. Builds and operates a personal "
            "lab (Proxmox, EVE-NG, Wazuh) \u2014 brings security awareness to every infrastructure "
            "task. Comfortable with scripting (Python, Bash, PowerShell) and automating routine "
            "admin work."
        ),
        jobs=[
            Job(
                title="System Administrator",
                period="2022 \u2013 2025 \u00b7 3 yrs",
                company="\u0418\u041f \u041e\u041e\u041e \u00abTNG LOGGING ASIA\u00bb",
                location="Tashkent",
                bullets=[
                    "Administered Windows Server (2016/2019) and Ubuntu/Debian servers; managed AD, GPO, DNS, DHCP for 50+ users",
                    "Set up and maintained Hyper-V and VMware virtualization clusters; handled VM provisioning and snapshot management",
                    "Configured and maintained network equipment (MikroTik, Cisco); managed firewall rules, VLANs, VPN tunnels",
                    "Implemented and monitored backup solutions; ensured system uptime and performed incident response",
                    "Wrote PowerShell and Bash scripts to automate user provisioning, log rotation, and patch deployment",
                ],
            ),
            Job(
                title="Cybersecurity Intern",
                period="Jan 2026 \u2013 Apr 2026",
                company="School 21 (\u041d\u041e\u0423 \u00abSHKOLA 21\u00bb)",
                location="Tashkent",
                bullets=[
                    "Deployed and hardened server infrastructure: Vaultwarden on Docker + Nginx Proxy Manager + automated backups",
                    "Configured Wazuh SIEM and Suricata IDS on Proxmox; wrote detection rules from live pcap analysis",
                    "Designed enterprise network topology (NGFW + WireGuard VPN + IDS + endpoint protection)",
                    "Configured \u041a\u0440\u0438\u043f\u0442\u043e\u041f\u0420\u041e NGate: GOST TLS, certificate generation, SCP key transfer, Windows CSP integration",
                    "Automated Wazuh agent deployment and removal across 50+ hosts using Python + paramiko over SSH",
                ],
            ),
        ],
        volunteering=[
            Job(
                title="IT Volunteer",
                period="2023 \u2013 2024",
                company="School 21 community",
                location="Tashkent",
                bullets=[
                    "Helped fellow students set up Linux workstations, VPN access, and lab virtual machines",
                    "Ran informal workshops on Active Directory basics and network troubleshooting",
                ],
            ),
        ],
        skill_groups=[
            SkillGroup(
                "OS & Servers",
                [
                    Skill("Windows Server 2016/19/22/25", True),
                    Skill("Ubuntu/Debian/Arch", True),
                    Skill("CentOS"),
                ],
            ),
            SkillGroup(
                "Directory & Services",
                [
                    Skill("Active Directory", True),
                    Skill("GPO", True),
                    Skill("DNS/DHCP", True),
                    Skill("Nginx"),
                ],
            ),
            SkillGroup(
                "Virtualization",
                [
                    Skill("Proxmox", True),
                    Skill("Hyper-V", True),
                    Skill("GNS3", True),
                    Skill("VMware Workstation"),
                    Skill("EVE-NG"),
                ],
            ),
            SkillGroup(
                "Scripting",
                [
                    Skill("Python", True),
                    Skill("Bash", True),
                    Skill("PowerShell", True),
                    Skill("Git"),
                ],
            ),
            SkillGroup(
                "Networking",
                [
                    Skill("MikroTik", True),
                    Skill("Cisco IOS", True),
                    Skill("VLANs"),
                    Skill("VPN/WireGuard"),
                    Skill("IPsec"),
                ],
            ),
            SkillGroup(
                "Security (bonus)",
                [
                    Skill("Wazuh SIEM"),
                    Skill("Suricata IDS"),
                    Skill("MITRE ATT&CK"),
                    Skill("Docker"),
                ],
            ),
        ],
        soft_skills=[
            "Problem Solving",
            "Self-learning",
            "Communication",
            "Time Management",
            "Attention to Detail",
            "Technical Documentation",
            "Team Collaboration",
        ],
        education=[
            Education(
                "School 21 \u2014 Cybersecurity Program",
                "\u041d\u041e\u0423 \u00abSHKOLA 21\u00bb, Tashkent \u00b7 2026",
                highlight=True,
            ),
            Education("CompTIA Security+", "Planned \u00b7 Priority #1"),
            Education("eJPT", "Planned \u00b7 Offensive track"),
        ],
        lab_bullets=[
            "Proxmox hypervisor running EVE-NG (Cisco/MikroTik/Juniper topologies), Wazuh SIEM, and Suricata IDS for continuous practice",
            "Vaultwarden self-hosted deployment: Docker, Nginx Proxy Manager, automated SQLite backups (Bash + Python)",
            "Mass SSH automation: paramiko-based scripts for fleet-wide agent deployment/removal across 50+ Ubuntu hosts",
        ],
    )
