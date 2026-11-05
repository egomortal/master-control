#include "i18n.h"

#include <stdio.h>
#include <string.h>

static const char *const k_codes[LANG_COUNT] = { "en", "ar", "es", "fr", "de", "pt", "ru", "ja", "zh", "it", "he" };

static const char *const k_text[LANG_COUNT][MSG_COUNT] = {
    {   /* en */
        "Master Control: %s connected (slot %d)",
        "Master Control: %s connected (slot %d), no virtual pad - see the log",
        "Master Control: %s disconnected (slot %d)",
        "Master Control: Bluetooth ready (%d paired)",
        "Master Control: Bluetooth not responding - open the menu for details",
        "Master Control: Bluetooth lost - press Retry in the menu",
        "Master Control: pairing for %d s - put the controller in pairing mode",
        "Master Control: already running",
        "Master Control %s ready - menu: %s",
        "Master Control %s - menu unavailable (port %d busy?)",
        "Master Control: cannot create virtual controllers - see the menu or the log",
        "Master Control: paused for rest mode - resumes by itself after waking",
        "Master Control: stopped",
        "Master Control: controller paired! Turn it off and on once - from now on it connects by itself when switched on",
        "Master Control: to keep playing without the cable, hold the controller's pair button until it blinks fast (first time only, %d s)",
        "Master Control: no controller paired yet - hold your controller's pair button until it blinks fast (%d s)",
        "Controller",
    },
    {   /* ar */
        "Master Control: تم توصيل %s (الخانة %d)",
        "Master Control: تم توصيل %s (الخانة %d) دون وحدة تحكم افتراضية - راجع السجل",
        "Master Control: تم فصل %s (الخانة %d)",
        "Master Control: البلوتوث جاهز (وحدات تحكم مقترنة: %d)",
        "Master Control: البلوتوث لا يستجيب - افتح القائمة لمعرفة التفاصيل",
        "Master Control: انقطع البلوتوث - اضغط «إعادة المحاولة» في القائمة",
        "Master Control: الاقتران لمدة %d ثانية - ضع وحدة التحكم في وضع الاقتران",
        "Master Control: يعمل بالفعل",
        "Master Control %s جاهز - القائمة: %s",
        "Master Control %s - القائمة غير متاحة (هل المنفذ %d مشغول؟)",
        "Master Control: تعذّر إنشاء وحدات تحكم افتراضية - راجع القائمة أو السجل",
        "Master Control: متوقف مؤقتًا بسبب وضع الراحة - يستأنف تلقائيًا بعد تنشيط الجهاز",
        "Master Control: تم الإيقاف",
        "Master Control: تم اقتران وحدة التحكم! أطفئها ثم شغّلها مرة واحدة - من الآن ستتصل تلقائيًا عند تشغيلها",
        "Master Control: لمواصلة اللعب دون كابل، اضغط مطولًا على زر الاقتران في وحدة التحكم حتى يومض بسرعة (المرة الأولى فقط، %d ثانية)",
        "Master Control: لا توجد وحدة تحكم مقترنة بعد - اضغط مطولًا على زر الاقتران في وحدة التحكم حتى يومض بسرعة (%d ثانية)",
        "وحدة التحكم",
    },
    {   /* es */
        "Master Control: %s conectado (ranura %d)",
        "Master Control: %s conectado (ranura %d), sin mando virtual - consulta el registro",
        "Master Control: %s desconectado (ranura %d)",
        "Master Control: Bluetooth listo (%d emparejados)",
        "Master Control: el Bluetooth no responde - abre el menú para ver los detalles",
        "Master Control: se perdió el Bluetooth - pulsa Reintentar en el menú",
        "Master Control: emparejando durante %d s - pon el mando en modo de emparejamiento",
        "Master Control: ya se está ejecutando",
        "Master Control %s listo - menú: %s",
        "Master Control %s - menú no disponible (¿puerto %d ocupado?)",
        "Master Control: no se pueden crear mandos virtuales - consulta el menú o el registro",
        "Master Control: en pausa por el modo de reposo - se reanuda solo cuando la consola se active",
        "Master Control: detenido",
        "Master Control: ¡mando emparejado! Apágalo y enciéndelo una vez - a partir de ahora se conecta solo al encenderlo",
        "Master Control: para seguir jugando sin cable, mantén pulsado el botón de emparejamiento del mando hasta que parpadee rápido (solo la primera vez, %d s)",
        "Master Control: todavía no hay ningún mando emparejado - mantén pulsado el botón de emparejamiento del mando hasta que parpadee rápido (%d s)",
        "Mando",
    },
    {   /* fr */
        "Master Control : %s connectée (emplacement %d)",
        "Master Control : %s connectée (emplacement %d), sans manette virtuelle - voir le journal",
        "Master Control : %s déconnectée (emplacement %d)",
        "Master Control : Bluetooth prêt (manettes associées : %d)",
        "Master Control : le Bluetooth ne répond pas - ouvrez le menu pour plus de détails",
        "Master Control : Bluetooth perdu - appuyez sur Réessayer dans le menu",
        "Master Control : association pendant %d s - mettez la manette en mode association",
        "Master Control : déjà en cours d'exécution",
        "Master Control %s prêt - menu : %s",
        "Master Control %s - menu indisponible (port %d occupé ?)",
        "Master Control : impossible de créer les manettes virtuelles - voir le menu ou le journal",
        "Master Control : en pause pour le mode repos - reprend tout seul à la sortie du mode repos",
        "Master Control : arrêté",
        "Master Control : manette associée ! Éteignez-la puis rallumez-la une fois - elle se connectera ensuite toute seule à l'allumage",
        "Master Control : pour continuer à jouer sans câble, maintenez le bouton d'association de la manette jusqu'à ce qu'il clignote rapidement (la première fois seulement, %d s)",
        "Master Control : aucune manette associée pour l'instant - maintenez le bouton d'association de la manette jusqu'à ce qu'il clignote rapidement (%d s)",
        "Manette",
    },
    {   /* de */
        "Master Control: %s verbunden (Slot %d)",
        "Master Control: %s verbunden (Slot %d), kein virtueller Controller - siehe Protokoll",
        "Master Control: %s getrennt (Slot %d)",
        "Master Control: Bluetooth bereit (%d gekoppelt)",
        "Master Control: Bluetooth antwortet nicht - Details im Menü",
        "Master Control: Bluetooth-Verbindung verloren - im Menü auf „Erneut versuchen“ drücken",
        "Master Control: Kopplung für %d s - Controller in den Kopplungsmodus versetzen",
        "Master Control: läuft bereits",
        "Master Control %s bereit - Menü: %s",
        "Master Control %s - Menü nicht verfügbar (Port %d belegt?)",
        "Master Control: virtuelle Controller können nicht erstellt werden - siehe Menü oder Protokoll",
        "Master Control: wegen Ruhemodus pausiert - setzt nach dem Aufwecken von selbst fort",
        "Master Control: beendet",
        "Master Control: Controller gekoppelt! Einmal aus- und wieder einschalten - ab jetzt verbindet er sich beim Einschalten von selbst",
        "Master Control: Um ohne Kabel weiterzuspielen, die Kopplungstaste des Controllers gedrückt halten, bis er schnell blinkt (nur beim ersten Mal, %d s)",
        "Master Control: Noch kein Controller gekoppelt - die Kopplungstaste des Controllers gedrückt halten, bis er schnell blinkt (%d s)",
        "Controller",
    },
    {   /* pt */
        "Master Control: %s conectado (slot %d)",
        "Master Control: %s conectado (slot %d), sem controle virtual - veja o log",
        "Master Control: %s desconectado (slot %d)",
        "Master Control: Bluetooth pronto (%d pareados)",
        "Master Control: o Bluetooth não responde - abra o menu para ver os detalhes",
        "Master Control: Bluetooth perdido - pressione Tentar novamente no menu",
        "Master Control: pareando por %d s - coloque o controle no modo de pareamento",
        "Master Control: já está em execução",
        "Master Control %s pronto - menu: %s",
        "Master Control %s - menu indisponível (porta %d ocupada?)",
        "Master Control: não foi possível criar controles virtuais - veja o menu ou o log",
        "Master Control: pausado pelo modo de repouso - retoma sozinho depois que o console despertar",
        "Master Control: parado",
        "Master Control: controle pareado! Desligue e ligue uma vez - a partir de agora ele se conecta sozinho ao ser ligado",
        "Master Control: para continuar jogando sem o cabo, segure o botão de pareamento do controle até ele piscar rápido (só na primeira vez, %d s)",
        "Master Control: nenhum controle pareado ainda - segure o botão de pareamento do controle até ele piscar rápido (%d s)",
        "Controle",
    },
    {   /* ru */
        "Master Control: %s подключён (слот %d)",
        "Master Control: %s подключён (слот %d), без виртуального геймпада - см. журнал",
        "Master Control: %s отключён (слот %d)",
        "Master Control: Bluetooth готов (сопряжено: %d)",
        "Master Control: Bluetooth не отвечает - подробности в меню",
        "Master Control: связь Bluetooth потеряна - нажмите «Повторить» в меню",
        "Master Control: сопряжение %d с - переведите геймпад в режим сопряжения",
        "Master Control: уже запущен",
        "Master Control %s готов - меню: %s",
        "Master Control %s - меню недоступно (порт %d занят?)",
        "Master Control: не удалось создать виртуальные геймпады - см. меню или журнал",
        "Master Control: приостановлен из-за режима покоя - продолжит работу сам после выхода из него",
        "Master Control: остановлен",
        "Master Control: геймпад сопряжён! Выключите и включите его один раз - дальше он будет подключаться сам при включении",
        "Master Control: чтобы играть без кабеля, удерживайте кнопку сопряжения на геймпаде, пока он не начнёт быстро мигать (только в первый раз, %d с)",
        "Master Control: пока нет сопряжённых геймпадов - удерживайте кнопку сопряжения на геймпаде, пока он не начнёт быстро мигать (%d с)",
        "Геймпад",
    },
    {   /* ja */
        "Master Control: %s が接続されました（スロット %d）",
        "Master Control: %s が接続されました（スロット %d）。仮想コントローラーなし - ログを確認してください",
        "Master Control: %s の接続が切れました（スロット %d）",
        "Master Control: Bluetooth 準備完了（ペアリング済み %d 台）",
        "Master Control: Bluetooth が応答しません - 詳細はメニューで確認してください",
        "Master Control: Bluetooth が切断されました - メニューの「再試行」を押してください",
        "Master Control: %d 秒間ペアリング中 - コントローラーをペアリングモードにしてください",
        "Master Control: すでに実行中です",
        "Master Control %s 準備完了 - メニュー: %s",
        "Master Control %s - メニューを開けません（ポート %d が使用中？）",
        "Master Control: 仮想コントローラーを作成できません - メニューかログを確認してください",
        "Master Control: レストモードのため一時停止しました - 復帰後に自動で再開します",
        "Master Control: 停止しました",
        "Master Control: コントローラーをペアリングしました！一度電源を切って入れ直してください - 以後は電源を入れるだけで自動接続します",
        "Master Control: ケーブルなしで遊び続けるには、コントローラーのペアリングボタンを速く点滅するまで長押ししてください（初回のみ、%d 秒）",
        "Master Control: ペアリング済みのコントローラーがまだありません - コントローラーのペアリングボタンを速く点滅するまで長押ししてください（%d 秒）",
        "コントローラー",
    },
    {   /* zh */
        "Master Control：%s 已连接（槽位 %d）",
        "Master Control：%s 已连接（槽位 %d），但没有虚拟手柄 - 请查看日志",
        "Master Control：%s 已断开（槽位 %d）",
        "Master Control：蓝牙已就绪（已配对 %d 个）",
        "Master Control：蓝牙无响应 - 请打开菜单查看详情",
        "Master Control：蓝牙连接丢失 - 请在菜单中按“重试”",
        "Master Control：正在配对，持续 %d 秒 - 请让手柄进入配对模式",
        "Master Control：已在运行",
        "Master Control %s 已就绪 - 菜单：%s",
        "Master Control %s - 菜单不可用（端口 %d 被占用？）",
        "Master Control：无法创建虚拟手柄 - 请查看菜单或日志",
        "Master Control：因进入静止模式已暂停 - 唤醒后将自动恢复",
        "Master Control：已停止",
        "Master Control：手柄已配对！请关闭再打开一次 - 以后开机即可自动连接",
        "Master Control：如需拔掉数据线继续游戏，请按住手柄的配对按钮直到快速闪烁（仅首次需要，%d 秒）",
        "Master Control：尚未配对任何手柄 - 请按住手柄的配对按钮直到快速闪烁（%d 秒）",
        "手柄",
    },
    {   /* it */
        "Master Control: %s connesso (slot %d)",
        "Master Control: %s connesso (slot %d), nessun controller virtuale - vedi il registro",
        "Master Control: %s disconnesso (slot %d)",
        "Master Control: Bluetooth pronto (%d associati)",
        "Master Control: il Bluetooth non risponde - apri il menu per i dettagli",
        "Master Control: Bluetooth perso - premi Riprova nel menu",
        "Master Control: associazione per %d s - metti il controller in modalità di associazione",
        "Master Control: già in esecuzione",
        "Master Control %s pronto - menu: %s",
        "Master Control %s - menu non disponibile (porta %d occupata?)",
        "Master Control: impossibile creare i controller virtuali - vedi il menu o il registro",
        "Master Control: in pausa per la modalità di riposo - riprende da solo dopo il risveglio",
        "Master Control: arrestato",
        "Master Control: controller associato! Spegnilo e riaccendilo una volta - da ora si collega da solo quando lo accendi",
        "Master Control: per continuare a giocare senza cavo, tieni premuto il tasto di associazione del controller finché non lampeggia velocemente (solo la prima volta, %d s)",
        "Master Control: ancora nessun controller associato - tieni premuto il tasto di associazione del controller finché non lampeggia velocemente (%d s)",
        "Controller",
    },
    {   /* he */
        "Master Control: %s מחובר (חריץ %d)",
        "Master Control: %s מחובר (חריץ %d), בלי בקר וירטואלי - ראה את היומן",
        "Master Control: %s התנתק (חריץ %d)",
        "Master Control: הבלוטות׳ מוכן (מצומדים: %d)",
        "Master Control: הבלוטות׳ לא מגיב - פתח את התפריט לפרטים",
        "Master Control: הבלוטות׳ נקטע - לחץ «נסה שוב» בתפריט",
        "Master Control: צימוד ל-%d שנ׳ - העבר את הבקר למצב צימוד",
        "Master Control: כבר רץ",
        "Master Control %s מוכן - תפריט: %s",
        "Master Control %s - התפריט לא זמין (פורט %d תפוס?)",
        "Master Control: לא ניתן ליצור בקרי משחק וירטואליים - ראה את התפריט או את היומן",
        "Master Control: מושהה בגלל מצב מנוחה - ימשיך מעצמו אחרי ההתעוררות",
        "Master Control: נעצר",
        "Master Control: הבקר צומד! כבה והדלק אותו פעם אחת - מעכשיו הוא יתחבר מעצמו כשתדליק אותו",
        "Master Control: כדי להמשיך לשחק בלי כבל, החזק את לחצן הצימוד של הבקר עד שהוא מהבהב מהר (רק בפעם הראשונה, %d שנ׳)",
        "Master Control: עדיין אין בקר מצומד - החזק את לחצן הצימוד של הבקר עד שהוא מהבהב מהר (%d שנ׳)",
        "בקר",
    },
};

static int g_lang;

int i18n_index(const char *code)
{
    int i;

    if (!code) return -1;
    for (i = 0; i < LANG_COUNT; i++)
        if (strcmp(code, k_codes[i]) == 0) return i;
    return -1;
}

const char *i18n_code(int lang)
{
    return k_codes[lang >= 0 && lang < LANG_COUNT ? lang : 0];
}

int  i18n_get(void) { return g_lang; }

void i18n_set(int lang)
{
    g_lang = lang >= 0 && lang < LANG_COUNT ? lang : 0;
}

const char *tr_in(int lang, int msg)
{
    if (msg < 0 || msg >= MSG_COUNT) return "";
    if (lang < 0 || lang >= LANG_COUNT) lang = 0;
    return k_text[lang][msg];
}

const char *tr(int msg)
{
    return tr_in(g_lang, msg);
}

int i18n_load(const char *path)
{
    char buf[8] = "";
    FILE *f = fopen(path, "r");
    size_t n;

    if (f) {
        n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        buf[n] = '\0';
        buf[strcspn(buf, " \t\r\n")] = '\0';
    }
    i18n_set(i18n_index(buf));
    return g_lang;
}

int i18n_save(const char *path)
{
    char tmp[300];
    FILE *f;
    int ok;

    snprintf(tmp, sizeof tmp, "%s.new", path);
    if (!(f = fopen(tmp, "w"))) return 0;
    ok = fprintf(f, "%s\n", k_codes[g_lang]) > 0;
    if (fclose(f) != 0) ok = 0;
    if (!ok || rename(tmp, path) != 0) {
        remove(tmp);
        return 0;
    }
    return 1;
}
