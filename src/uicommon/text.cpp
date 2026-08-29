#include "text.h"

namespace cvb {
namespace uicommon {

const char* const kThemeRoles[TR_Count] = {
    "Фон страницы",
    "Разделители",
    "Заголовки (имя, должности)",
    "Основной текст",
    "Вторичный текст (компания, контакты)",
    "Даты и неактивные записи",
    "Акцент 1 (секции, маркеры)",
    "Акцент 2 (soft skills, образование)",
};

const char* const kSectionNames[kSectionCount] = {
    "О себе",
    "Учёба (подробно)",
    "Опыт работы",
    "Технические навыки",
    "Soft skills",
    "Сертификаты и курсы",
    "Личная лаборатория",
    "Волонтёрство",
};

const char* sectionName(const std::string& id) {
    for (int i = 0; i < kSectionCount; ++i)
        if (id == kSectionIds[i]) return kSectionNames[i];
    return "";
}

}  // namespace uicommon
}  // namespace cvb
