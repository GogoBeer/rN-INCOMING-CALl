# Translator Comment Policy

Translator comments are required for all user-facing strings. A PR introducing
new user-facing strings must also submit the accompanying translator comment.
To prevent this requirement from halting development, the PR author can specify
that they would like the introduction of the comment to be picked up in a
follow-up PR.

## Formulating Translator Comments

Translator comments only apply to strings that are marked for translation.
For more information, see:
[Internationalization and Localization with Qt Quick](https://doc.qt.io/qt-5/qtquick-internationalization.html)

Below, the values for the `text` and `description` properties are examples of
new user-facing strings marked for translation without translator comments:

```qml
OptionButton {
   ButtonGroup.group: group
   Layout.fillWidth: true
   text: qsTr("Only when on Wi-Fi")
   description: qsTr("Loads quickly when on wi-fi and pauses when on cellular data.")
}
```

Translator comments are needed to provide a translator with the appropriate
context needed to aid them with translating a certain string.
The first step in formulating a translator comment is to think about what
information a translator needs to perform their job effectively.

**Necessary context is**:
- Where is this string shown?
  - e.g. Shown in the Create Wallet dialog, Shown in file menu opti