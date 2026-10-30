#include "NoteEditorActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "util/VaultEdit.h"

namespace fui = freeink::ui;

NoteEditorActivity::NoteEditorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string notePath)
    : UiListActivity("NoteEditor", renderer, mappedInput), path(std::move(notePath)) {}

void NoteEditorActivity::onEnter() {
  UiListActivity::onEnter();
  title = FsHelpers::getFileNameWithoutExtension(path);
  std::string text;
  loaded = vaultedit::readNote(path, text);
  if (loaded) {
    loadedHash = vaultedit::contentHash(text);
    loadedSize = text.size();
    readOnly = !buffer.load(text);
  }
  rebuildRows();
}

int NoteEditorActivity::listCount() const {
  return ACTION_ROWS + static_cast<int>(std::max<size_t>(buffer.lineCount(), 1));
}

void NoteEditorActivity::rebuildRows() {
  const size_t lines = buffer.lineCount();
  rowItems.assign(static_cast<size_t>(listCount()), fui::ListItem{});
  numbers.assign(lines, "");
  rowItems[0].label = tr(STR_EDITOR_SAVE);
  rowItems[0].subtitle = buffer.changed() ? tr(STR_EDITOR_UNSAVED) : nullptr;
  char text[48];
  snprintf(text, sizeof(text), "%s (%u)", tr(STR_EDITOR_UNDO), static_cast<unsigned>(buffer.undoDepth()));
  undoLabel = text;
  rowItems[1].label = undoLabel.c_str();
  if (lines == 0) {
    rowItems[ACTION_ROWS].label = tr(STR_EDITOR_ADD_LINE);
  }
  for (size_t i = 0; i < lines; ++i) {
    auto& item = rowItems[ACTION_ROWS + i];
    numbers[i] = std::to_string(i + 1);
    // An empty line still needs something to tap.
    item.label = buffer.line(i).empty() ? tr(STR_EDITOR_EMPTY_LINE) : buffer.line(i).c_str();
    item.value = numbers[i].c_str();
  }
  for (size_t i = 0; i < rowItems.size(); ++i) rowItems[i].actionValue = static_cast<int16_t>(i);
  requestUpdate();
}

void NoteEditorActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (!loaded || readOnly) {
    auto text = screen.theme().bodyText;
    text.maxLines = 4;
    const auto box = screen.body().inset(fui::Insets{0, 16, 0, 16});
    screen.target().text(fui::Rect{box.x, box.y, box.width, 70},
                         I18N.get(!loaded ? StrId::STR_EDITOR_CANT_OPEN : StrId::STR_EDITOR_READ_ONLY), text);
    screen.spacer(76);
    if (!loaded) return;
  }
  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  fui::TextStyle label = screen.theme().smallText;
  label.maxLines = 3;  // a long line shows wrapped; it is still one line to edit
  props.labelText = label;
  syncListViewport(screen, props);
  screen.list(props);
}

void NoteEditorActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

bool NoteEditorActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void NoteEditorActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (!loaded) return;
  if (index == 0) {
    save();
    return;
  }
  if (index == 1) {
    if (buffer.undo()) {
      nav.selected = ACTION_ROWS + static_cast<int>(buffer.lastLine());
      rebuildRows();
    }
    return;
  }
  if (readOnly) return;
  if (buffer.lineCount() == 0) {
    editLine(0, true, 0);
    return;
  }
  lineActions(static_cast<size_t>(index - ACTION_ROWS));
}

void NoteEditorActivity::lineActions(const size_t line) {
  static constexpr StrId OPTIONS[] = {StrId::STR_EDITOR_EDIT,         StrId::STR_EDITOR_INSERT_ABOVE,
                                      StrId::STR_EDITOR_INSERT_BELOW, StrId::STR_EDITOR_DELETE_LINE,
                                      StrId::STR_EDITOR_MOVE_UP,      StrId::STR_EDITOR_MOVE_DOWN,
                                      StrId::STR_EDITOR_JOIN_NEXT};
  // "Join with next line" is left out for the last line.
  optionPopup.show(
      StrId::STR_EDITOR_LINE, OPTIONS, line + 1 < buffer.lineCount() ? 7 : 6, 0, [this, line](const int choice) {
        bool changedNow = false;
        switch (choice) {
          case 0:
            editLine(line, false, line);
            return;
          case 1:
            editLine(line, true, line);
            return;
          case 2:
            editLine(line, true, line + 1);
            return;
          case 3:
            changedNow = buffer.remove(line);
            break;
          case 4:
            changedNow = buffer.moveUp(line);
            break;
          case 5:
            changedNow = buffer.moveDown(line);
            break;
          case 6:
            changedNow = buffer.join(line);
            break;
          default:
            break;
        }
        if (changedNow) {
          // After deleting the last line, the line before it (or "Add a line" in an empty note).
          const size_t count = buffer.lineCount();
          nav.selected = ACTION_ROWS + static_cast<int>(count == 0 ? 0 : std::min(buffer.lastLine(), count - 1));
          rebuildRows();
        } else {
          requestUpdate();
        }
      });
  requestUpdate();
}

// The keyboard for one line: the line itself (insertAt false) or a new line placed at `at`.
void NoteEditorActivity::editLine(const size_t line, const bool insertAt, const size_t at) {
  if (insertAt ? at > buffer.lineCount() : line >= buffer.lineCount()) return;
  const std::string initial = insertAt ? "" : buffer.line(line);
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_EDITOR_LINE), initial,
                                                           LineBuffer::MAX_LINE_BYTES, InputType::Text);
  if (!keyboard) return;
  startActivityForResult(std::move(keyboard), [this, line, insertAt, at](const ActivityResult& result) {
    if (!result.isCancelled) {
      std::string text = std::get<KeyboardResult>(result.data).text;
      bool ok;
      if (insertAt) {
        ok = buffer.insert(at, std::move(text));
      } else {
        ok = buffer.replace(line, std::move(text));
      }
      if (ok) nav.selected = ACTION_ROWS + static_cast<int>(insertAt ? at : line);
    }
    rebuildRows();
  });
}

void NoteEditorActivity::save() {
  if (!buffer.changed()) {
    finish();
    return;
  }
  const auto r = vaultedit::saveNote(path, buffer.serialize(), loadedHash, loadedSize);
  if (r == vaultedit::Result::Ok) {
    ActivityResult res;
    res.isCancelled = false;
    setResult(std::move(res));
    finish();
    return;
  }
  showMessage(
      I18N.get(r == vaultedit::Result::Changed ? StrId::STR_EDITOR_CHANGED_ON_CARD : StrId::STR_NOTE_SAVE_FAILED));
}

void NoteEditorActivity::showMessage(const char* text) {
  const std::string ok = tr(STR_OK_BUTTON);
  if (auto dialog = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, text, title, ok, ok)) {
    startActivityForResult(std::move(dialog), [this](const ActivityResult&) { requestUpdate(); });
  }
}

void NoteEditorActivity::onBackButton() {
  if (!buffer.changed()) {
    finish();
    return;
  }
  auto dialog = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_EDITOR_DISCARD), title,
                                                        tr(STR_EDITOR_KEEP_EDITING), tr(STR_EDITOR_DISCARD_YES));
  if (!dialog) return;
  startActivityForResult(std::move(dialog), [this](const ActivityResult& result) {
    if (!result.isCancelled) {
      finish();
    } else {
      requestUpdate();
    }
  });
}
