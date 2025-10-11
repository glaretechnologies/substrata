# 🌿 Ветка: feature/dynamic-parcel-links

## 🎯 **Назначение**
Pull Request в upstream репозиторий [glaretechnologies/substrata](https://github.com/glaretechnologies/substrata) для добавления динамических ссылок в редакторе парселей.

## 📋 **Что содержит**
- **ParcelEditor.h** - добавлен метод `setCurrentServerURL()` и переменная `current_server_url`
- **ParcelEditor.cpp** - реализована динамическая смена ссылок в зависимости от текущего сервера
- **Автоматическое обновление** ссылок при смене сервера (substrata.info ↔ vr.metasiberia.com)

## 🔗 **Ссылки**
- **Pull Request:** https://github.com/glaretechnologies/substrata/pull/16
- **Upstream:** glaretechnologies/substrata
- **Fork:** shipilovden/substrata-metasiberia

## 📊 **Статус**
- ✅ **Готово** - код написан и протестирован
- ✅ **Отправлено** - PR создан в upstream
- ⏳ **Ожидает** - review от разработчиков Glare Technologies

## 🚀 **Следующие шаги**
1. Дождаться review от upstream
2. Внести изменения по замечаниям (если есть)
3. После принятия - удалить ветку

## 💡 **Польза для сообщества**
- Улучшает UX для пользователей custom серверов
- Обратная совместимость с substrata.info
- Простая реализация без breaking changes