# cmd accepted-base smart engine

هذه النسخة مبنية على أساس النسخة المقبولة عند الجهاز:

- Artifact: `cmd_final_release`
- Base commit: `be8d964b55a0d28b6522cc1a77e6661b7acb4876`
- Previous digest: `sha256:fd8ca7aa822b1817d73889a0fb0618e9e7d6968ed0e64455e5f0c902f0bd135c`

## التعديلات

- تحسين قراءة الكيبورد بنفس فكرة Beeftext: استخدام لغة الإدخال للنافذة النشطة مع `GetKeyState` و `ToUnicodeEx`.
- تحسين التطبيع العربي: الألف بأشكاله، الياء/الألف المقصورة، التاء المربوطة، الهمزات، التشكيل، التطويل، والأرقام العربية/الفارسية.
- فهرس سريع للكيووردات: أطول تطابق أولًا بدون مسح كل الكاندات مع كل ضغطه.
- إصلاح مشكلة `2.` عبر تحرير مفاتيح Ctrl/Alt/Shift قبل الحذف، واستخدام طول الكيوورد الأصلي كحد أدنى للحذف.
- الإعداد الافتراضي أصبح Automatic مثل Beeftext: `keyword + Space / Enter / Tab`.

## الاستخدام

بعد GitHub Actions حمّل Artifact باسم `cmd_final_release` وشغل `cmd.exe` مع إبقاء `snippets.json` بجواره.

لاستيراد كل كاندات Beeftext، استخدم زر Import واختر `snippets_converted_from_beeftext.json` أو ملف `Beeftext.btbackup`.
