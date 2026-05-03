# chrome Text Expander v22

نسخة مستقلة باسم `chrome.exe` مبنية بمنطق أقرب لطريقة Beeftext في قراءة ضغطات الكيبورد والتوسيع:

- يستخدم `WH_KEYBOARD_LL` مثل Beeftext.
- يستخدم `GetKeyState` للـ modifiers و `ToUnicodeEx(..., 1<<2, active HKL)` بنفس فكرة Beeftext الحديثة.
- يقرأ `snippets.json` مباشرة، ولو مش موجود يحاول يقرأ `Beeftext.btbackup`.
- يدعم الكيورد العربي والإنجليزي والأرقام والرموز.
- يدعم التوسيع بـ `keyword + Space / Enter / Tab` أو `Ctrl + Space`.
- يدعم immediate expansion للكلمات المنتهية بعلامات مثل `2.` بدون ما يسيب الرقم.
- يحتوي على `snippets.json` محوّل من ملف Beeftext المرفق بعدد الكاندات الحقيقي.

## Build

على GitHub Actions هيطلع artifact باسم:

`chrome_v22_beeftext_style_engine_release`

أو محليًا من Developer Command Prompt:

```bat
build.bat
```

الناتج:

`publish\chrome.exe`
