#include "MainWindow.h"
#include "SettingsDialog.h"
#include "TitleBar.h"
#include "ForjDialog.h"
#include "ForjPrompt.h"
#include "VoiceEngine.h"

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialog>
#include <QStatusBar>
#include <QToolBar>
#include <QAction>
#include <QDateTime>
#include <QScrollBar>
#include <QListWidgetItem>
#include <QSplitter>
#include <QMenu>
#include <QCompleter>
#include <QStringListModel>
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QPainter>
#include <QPainterPath>
#include <QBuffer>
#include <QStyledItemDelegate>
#include <QFont>
#include <QEvent>
#include <QContextMenuEvent>
#include <QUrl>
#include <QRegularExpression>
#include <QSystemTrayIcon>
#include <QMessageBox>

// ── Avatar rendering helpers ──────────────────────────────────────────────────

// Returns a circular PNG (size×size) from raw image bytes, cover-scaled and
// center-cropped. Returns {} on failure.
static QByteArray makeCircularPng(const QByteArray& data, int size)
{
    QPixmap src;
    if (!src.loadFromData(data) || src.isNull())
        return {};

    // Scale to cover size×size without distortion
    QPixmap scaled = src.scaled(size, size, Qt::KeepAspectRatioByExpanding,
                                 Qt::SmoothTransformation);
    const int ox = (scaled.width()  - size) / 2;
    const int oy = (scaled.height() - size) / 2;

    QPixmap circular(size, size);
    circular.fill(Qt::transparent);
    QPainter p(&circular);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addEllipse(0, 0, size, size);
    p.setClipPath(path);
    p.drawPixmap(0, 0, scaled, ox, oy, size, size);
    p.end();

    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    circular.save(&buf, "PNG");
    return out;
}

// Returns an HTML <img> tag with a pre-rendered circular PNG, or {} on failure.
static QString circularAvatarHtml(const QByteArray& data, int size)
{
    const QByteArray png = makeCircularPng(data, size);
    if (png.isEmpty()) return {};
    return QStringLiteral(
        "<img src='data:image/png;base64,%1' width='%2' height='%2' "
        "style='vertical-align:top;'/>")
        .arg(QString::fromLatin1(png.toBase64()))
        .arg(size);
}

// Returns an HTML <img> tag of a solid-color circle (fallback when no avatar).
static QString colorCircleHtml(const QString& color, int size)
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(color));
    p.setPen(Qt::NoPen);
    p.drawEllipse(0, 0, size, size);
    p.end();

    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    pix.save(&buf, "PNG");
    return QStringLiteral(
        "<img src='data:image/png;base64,%1' width='%2' height='%2' "
        "style='vertical-align:top;'/>")
        .arg(QString::fromLatin1(out.toBase64()))
        .arg(size);
}

// ── Member list delegate ──────────────────────────────────────────────────────
// Roles for item data storage
static const int RoleNameRole   = Qt::UserRole;
static const int RoleColorRole  = Qt::UserRole + 1;
static const int kStatusDotRole = Qt::UserRole + 2;  // QString: online/idle/dnd/offline

// Small filled circle pixmap for status indicators
static QPixmap makeStatusDot(const QString& status, int size)
{
    QColor c;
    if      (status == "online") c = QColor(0x3B, 0xA5, 0x5C);
    else if (status == "idle")   c = QColor(0xFA, 0xA6, 0x1A);
    else if (status == "dnd")    c = QColor(0xED, 0x4C, 0x5E);
    else                         c = QColor(0x74, 0x7F, 0x8D);
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(c);
    p.setPen(Qt::NoPen);
    p.drawEllipse(0, 0, size, size);
    return pix;
}

class MemberDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
    { return QSize(0, 46); }

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override
    {
        p->save();
        if (opt.state & QStyle::State_Selected)
            p->fillRect(opt.rect, opt.palette.highlight());

        const int pad    = 5;
        const int iconSz = 34;
        const QRect iconRect(opt.rect.left() + pad,
                             opt.rect.top() + (opt.rect.height() - iconSz) / 2,
                             iconSz, iconSz);

        // Rounded avatar
        QIcon icon = idx.data(Qt::DecorationRole).value<QIcon>();
        if (!icon.isNull()) {
            QPixmap src = icon.pixmap(iconSz, iconSz);
            QPixmap rounded(iconSz, iconSz);
            rounded.fill(Qt::transparent);
            QPainter rp(&rounded);
            rp.setRenderHint(QPainter::Antialiasing);
            QPainterPath path;
            path.addEllipse(0, 0, iconSz, iconSz);
            rp.setClipPath(path);
            rp.drawPixmap(0, 0, src);
            p->drawPixmap(iconRect, rounded);
        }

        // Status dot — bottom-right corner of avatar (Discord-style)
        {
            const QString dotStatus = idx.data(kStatusDotRole).toString();
            const int dotR = 5;
            const int dotX = iconRect.right()  - dotR;
            const int dotY = iconRect.bottom() - dotR;
            // Background ring (hides avatar edge)
            p->setPen(Qt::NoPen);
            p->setBrush(opt.palette.window());
            p->drawEllipse(QPoint(dotX, dotY), dotR + 2, dotR + 2);
            // Colored status fill
            QColor dc;
            if      (dotStatus == "online") dc = QColor(0x3B, 0xA5, 0x5C);
            else if (dotStatus == "idle")   dc = QColor(0xFA, 0xA6, 0x1A);
            else if (dotStatus == "dnd")    dc = QColor(0xED, 0x4C, 0x5E);
            else                            dc = QColor(0x74, 0x7F, 0x8D);
            p->setBrush(dc);
            p->drawEllipse(QPoint(dotX, dotY), dotR, dotR);
        }

        const int textX  = iconRect.right() + pad + 2;
        const int textW  = opt.rect.right() - textX - pad;
        const QString name      = idx.data(Qt::DisplayRole).toString();
        const QString roleName  = idx.data(RoleNameRole).toString();
        const QString roleColor = idx.data(RoleColorRole).toString();

        const QColor nameColor = roleColor.isEmpty() ? opt.palette.text().color()
                                                     : QColor(roleColor);

        QFont bold = opt.font;
        bold.setBold(true);
        QFont small = opt.font;
        small.setPointSize(qMax(7, opt.font.pointSize() - 1));

        if (roleName.isEmpty()) {
            p->setFont(bold);
            p->setPen(nameColor);
            p->drawText(QRect(textX, opt.rect.top(), textW, opt.rect.height()),
                        Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, name);
        } else {
            const int half = opt.rect.height() / 2;
            p->setFont(bold);
            p->setPen(nameColor);
            p->drawText(QRect(textX, opt.rect.top() + 2, textW, half),
                        Qt::AlignBottom | Qt::AlignLeft | Qt::TextSingleLine, name);
            p->setFont(small);
            p->setPen(QColor(0x72, 0x76, 0x7D));
            p->drawText(QRect(textX, opt.rect.top() + half, textW, half - 2),
                        Qt::AlignTop | Qt::AlignLeft | Qt::TextSingleLine, roleName);
        }
        p->restore();
    }
};

// ── Voice tile avatar widget ───────────────────────────────────────────────────
// Custom widget that paints a circular avatar + green speaking ring.
// Ring is drawn as an outer filled ellipse; the avatar/initial is clipped to
// an inner circle on top of it.
class VoiceAvatarWidget final : public QWidget
{
public:
    static constexpr int kOuter = 88;                    // widget size (ring included)
    static constexpr int kRing  = 4;                     // ring width
    static constexpr int kInner = kOuter - 2 * kRing;    // avatar diameter = 80

    explicit VoiceAvatarWidget(const QString& username, const QColor& bg,
                               QWidget* parent = nullptr)
        : QWidget(parent), m_name(username), m_bg(bg)
    { setFixedSize(kOuter, kOuter); }

    void setAvatar(const QPixmap& pix) { m_pix = pix; update(); }
    void setSpeaking(bool s) { if (m_speaking != s) { m_speaking = s; update(); } }
    bool speaking() const { return m_speaking; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Outer green ring when speaking
        if (m_speaking) {
            p.setBrush(QColor(0x23, 0xa5, 0x5a));
            p.setPen(Qt::NoPen);
            p.drawEllipse(0, 0, kOuter, kOuter);
        }

        // Clip to inner avatar circle
        const QRect inner(kRing, kRing, kInner, kInner);
        QPainterPath clip;
        clip.addEllipse(inner);
        p.setClipPath(clip);

        if (!m_pix.isNull()) {
            p.drawPixmap(inner, m_pix);
        } else {
            // Fallback: solid color + initial letter
            p.setBrush(m_bg);
            p.setPen(Qt::NoPen);
            p.drawEllipse(inner);
            p.setClipping(false);
            p.setPen(Qt::white);
            QFont f;
            f.setPointSize(26);
            f.setBold(true);
            p.setFont(f);
            p.drawText(inner, Qt::AlignCenter,
                       m_name.isEmpty() ? QStringLiteral("?") : m_name.left(1).toUpper());
        }
    }

private:
    QString m_name;
    QColor  m_bg;
    QPixmap m_pix;
    bool    m_speaking{false};
};

// ── Avatar helpers ─────────────────────────────────────────────────────────────

static const QStringList kAvatarColors = {
    "#7289DA", "#43B581", "#FAA61A", "#F04747", "#593695",
    "#1E88E5", "#00ACC1", "#43A047", "#E53935", "#8E24AA",
    "#FF7043", "#26A69A", "#EF6C00", "#5C6BC0", "#00838F",
};

QString MainWindow::colorForId(int id)
{
    return kAvatarColors[qAbs(id) % kAvatarColors.size()];
}

// ── Constructor ───────────────────────────────────────────────────────────────

MainWindow::MainWindow(ApiClient* api, WebSocketClient* ws,
                       const UserInfo& me, QWidget* parent)
    : QMainWindow(parent)
    , m_api(api)
    , m_ws(ws)
    , m_me(me)
{
    setWindowTitle(QStringLiteral("Forj — %1").arg(me.username));
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    resize(1200, 720);
    buildUi();

    // WebSocket signals
    connect(m_ws, &WebSocketClient::newMessage,       this, &MainWindow::onNewMessage);
    connect(m_ws, &WebSocketClient::newDmMessage,    this, &MainWindow::onNewDmMessage);
    connect(m_ws, &WebSocketClient::userOnline,       this, &MainWindow::onUserOnline);
    connect(m_ws, &WebSocketClient::userOffline,      this, &MainWindow::onUserOffline);
    connect(m_ws, &WebSocketClient::userStatusChanged,this, &MainWindow::onUserStatusChanged);
    connect(m_ws, &WebSocketClient::mentioned,        this, &MainWindow::onMentioned);
    connect(m_ws, &WebSocketClient::friendRequest,    this, &MainWindow::onFriendRequest);
    connect(m_ws, &WebSocketClient::friendAccepted,   this, &MainWindow::onFriendAccepted);
    connect(m_ws, &WebSocketClient::guildInviteReceived, this, &MainWindow::onGuildInviteReceived);
    connect(m_ws, &WebSocketClient::voiceJoin,  this, &MainWindow::onVoiceJoin);
    connect(m_ws, &WebSocketClient::voiceLeave, this, &MainWindow::onVoiceLeave);
    connect(m_ws, &WebSocketClient::audioData,  this, [this](int /*channelId*/, int userId, const QByteArray& pcm) {
        if (m_voiceEngine) m_voiceEngine->playAudio(userId, pcm);
    });
    connect(m_ws, &WebSocketClient::voiceSpeaking, this, &MainWindow::onVoiceSpeaking);
    connect(m_ws, &WebSocketClient::kycApproved, this, [this] {
        QMessageBox::information(this,
            "Identity Verified",
            "Your identity has been verified. You now have full access to Forj.");
        // Reload guilds and DMs so the previously-403'd content appears
        loadGuilds();
        loadDms();
    });
    connect(m_ws, &WebSocketClient::safetyFreeze,
            this, [this](int /*convId*/, const QString& message) {
        QMessageBox::warning(this, "Forj Safety Notice", message);
    });
    connect(m_ws, &WebSocketClient::connected, this, [this] {
        statusBar()->showMessage("Connected", 3000);
    });
    connect(m_ws, &WebSocketClient::disconnected, this, [this] {
        statusBar()->showMessage("Disconnected — real-time updates paused");
    });

    loadGuilds();
    loadDms();

    // ── System tray icon ──────────────────────────────────────────────────────
    QPixmap trayPix(16, 16);
    trayPix.fill(Qt::transparent);
    {
        QPainter tp(&trayPix);
        tp.setRenderHint(QPainter::Antialiasing);
        tp.setBrush(QColor(0x58, 0x65, 0xF2));
        tp.setPen(Qt::NoPen);
        tp.drawEllipse(0, 0, 16, 16);
    }
    m_trayIcon = new QSystemTrayIcon(QIcon(trayPix), this);
    m_trayIcon->setToolTip("Forj");
    m_trayIcon->show();

    // ── Initialise own status from login response ──────────────────────────
    m_myStatus = m_me.status.isEmpty() ? "online" : m_me.status;
    updateMyStatusDot();
}

// ── UI construction ───────────────────────────────────────────────────────────

void MainWindow::buildUi()
{
    // ── Custom title bar (frameless window) ───────────────────────────────────
    m_titleBar = new TitleBar(this);
    m_titleBar->setTitle(windowTitle());
    setMenuWidget(m_titleBar);

    // ── Toolbar ───────────────────────────────────────────────────────────────
    auto* tb = addToolBar("Actions");
    tb->setMovable(false);
    tb->addAction("+ Server",  this, &MainWindow::onCreateGuildClicked);
    tb->addAction("Join",      this, &MainWindow::onJoinGuildClicked);
    tb->addSeparator();
    tb->addAction("+ DM",      this, &MainWindow::onOpenDmClicked);
    tb->addSeparator();

    auto* spacer = new QWidget; spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);
    tb->addAction("\U0001F465 Friends", this, &MainWindow::onFriendsClicked);
    tb->addSeparator();
    m_notifyAction = tb->addAction(QStringLiteral("\U0001F514"), this, &MainWindow::onNotifyClicked);
    m_notifyAction->setToolTip("Notifications");
    tb->addAction("\u2699 Settings", this, &MainWindow::onSettingsClicked);

    // ── Central splitter ──────────────────────────────────────────────────────
    m_splitter = new QSplitter(Qt::Horizontal, this);
    setCentralWidget(m_splitter);

    // 1. Left: Servers + DMs
    auto* leftPane   = new QWidget;
    auto* leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(4, 4, 4, 4);
    leftLayout->setSpacing(4);

    leftLayout->addWidget(new QLabel("<b style='color:#72767D;'>SERVERS</b>"));
    m_guildList = new QListWidget;
    m_guildList->setIconSize(QSize(36, 36));
    leftLayout->addWidget(m_guildList, 2);

    leftLayout->addWidget(new QLabel("<b style='color:#72767D;'>DIRECT MESSAGES</b>"));
    m_dmList = new QListWidget;
    leftLayout->addWidget(m_dmList, 1);

    // ── User status panel (bottom-left, like Discord) ─────────────────────────
    auto* userPanel = new QWidget;
    userPanel->setObjectName("forjUserPanel");
    userPanel->setStyleSheet(
        "#forjUserPanel { background:#1e2124; border-top:1px solid #111316; }");
    auto* upLayout = new QHBoxLayout(userPanel);
    upLayout->setContentsMargins(8, 6, 8, 6);
    upLayout->setSpacing(6);

    m_myStatusDotLabel = new QLabel(userPanel);
    m_myStatusDotLabel->setFixedSize(14, 14);

    auto* myNameLabel = new QLabel(m_me.username, userPanel);
    myNameLabel->setStyleSheet("color:#dcddde;font-size:12px;font-weight:bold;");
    myNameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* statusPickBtn = new QPushButton("\u25BE", userPanel);  // ▾
    statusPickBtn->setFlat(true);
    statusPickBtn->setFixedSize(24, 22);
    statusPickBtn->setStyleSheet(
        "QPushButton{color:#72767d;font-size:14px;border:none;}"
        "QPushButton:hover{color:#dcddde;}");
    statusPickBtn->setToolTip("Set status");

    upLayout->addWidget(m_myStatusDotLabel);
    upLayout->addWidget(myNameLabel, 1);
    upLayout->addWidget(statusPickBtn);
    leftLayout->addWidget(userPanel);

    // Status picker menu
    connect(statusPickBtn, &QPushButton::clicked, this, [this] {
        QMenu menu;
        struct { const char* label; const char* st; } opts[] = {
            { "\U0001F7E2 Online",          "online"    },
            { "\U0001F319 Idle",            "idle"      },
            { "\u26D4 Do Not Disturb",      "dnd"       },
            { "\U0001F47B Invisible",        "invisible" },
        };
        for (const auto& opt : opts) {
            const QString sVal = opt.st;
            const QString lbl  = (m_myStatus == sVal ? "\u2713 " : "   ") + QString(opt.label);
            menu.addAction(lbl, this, [this, sVal] {
                m_api->setStatus(sVal, [this, sVal](bool ok, const QString&) {
                    if (ok) {
                        m_myStatus = sVal;
                        updateMyStatusDot();
                    }
                });
            });
        }
        menu.exec(QCursor::pos());
    });

    m_splitter->addWidget(leftPane);

    // 2. Channel list
    auto* chanPane   = new QWidget;
    auto* chanLayout = new QVBoxLayout(chanPane);
    chanLayout->setContentsMargins(4, 4, 4, 4);
    m_guildNameLabel = new QLabel("<b>—</b>");
    chanLayout->addWidget(m_guildNameLabel);
    m_channelList    = new QListWidget;
    m_channelList->setContextMenuPolicy(Qt::CustomContextMenu);
    chanLayout->addWidget(m_channelList);
    m_addChannelBtn  = new QPushButton("+ Channel");
    m_addChannelBtn->setEnabled(false);
    chanLayout->addWidget(m_addChannelBtn);

    // Voice status bar
    buildVoiceBar();
    chanLayout->addWidget(m_voiceBar);

    m_splitter->addWidget(chanPane);

    // 3. Chat area
    auto* chatPane   = new QWidget;
    auto* chatLayout = new QVBoxLayout(chatPane);
    chatLayout->setContentsMargins(8, 8, 8, 8);
    chatLayout->setSpacing(4);
    m_messageView = new QTextBrowser;
    m_messageView->setOpenLinks(false);
    m_messageView->setOpenExternalLinks(false);
    m_messageView->setReadOnly(true);
    m_messageView->viewport()->installEventFilter(this);
    connect(m_messageView, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        if (url.scheme() == "forj" && url.host() == "user")
            showUserContextMenu(url.path().mid(1).toInt(), QCursor::pos());
        else if (url.scheme() == "forj" && url.host() == "reply") {
            const int msgId = url.path().mid(1).toInt();
            auto it = m_msgReplyCache.find(msgId);
            if (it != m_msgReplyCache.end()) {
                m_replyToMsgId    = msgId;
                m_replyToAuthor   = it->first;
                m_replyToContent  = it->second;
                const QString preview = m_replyToContent.left(60)
                    + (m_replyToContent.size() > 60 ? "…" : "");
                m_replyLabel->setText(
                    QStringLiteral("Replying to <b style='color:#5865F2;'>@%1</b>: %2")
                        .arg(m_replyToAuthor.toHtmlEscaped(),
                             preview.toHtmlEscaped()));
                m_replyBar->show();
                m_messageInput->setFocus();
            }
        }
    });
    chatLayout->addWidget(m_messageView, 1);

    // Reply bar (hidden until user clicks a reply button)
    m_replyBar = new QWidget;
    m_replyBar->setVisible(false);
    m_replyBar->setStyleSheet("background:#2f3136;border-top:1px solid #4f545c;padding:2px 4px;");
    auto* replyLayout = new QHBoxLayout(m_replyBar);
    replyLayout->setContentsMargins(6, 2, 6, 2);
    replyLayout->setSpacing(6);
    m_replyLabel = new QLabel;
    m_replyLabel->setStyleSheet("color:#dcddde;font-size:12px;");
    m_replyLabel->setTextFormat(Qt::RichText);
    replyLayout->addWidget(m_replyLabel, 1);
    m_replyCancelBtn = new QPushButton("✕");
    m_replyCancelBtn->setFixedSize(22, 22);
    m_replyCancelBtn->setStyleSheet("QPushButton{border:none;color:#72767d;font-weight:bold;}"
                                    "QPushButton:hover{color:#dcddde;}");
    replyLayout->addWidget(m_replyCancelBtn);
    chatLayout->addWidget(m_replyBar);

    auto* inputRow = new QHBoxLayout;
    m_messageInput = new QLineEdit;
    m_messageInput->setPlaceholderText("Message…  (type / for commands)");
    m_messageInput->setMaxLength(4000);
    m_messageInput->setEnabled(false);
    m_sendBtn = new QPushButton("Send");
    m_sendBtn->setFixedWidth(70);
    m_sendBtn->setEnabled(false);
    inputRow->addWidget(m_messageInput, 1);
    inputRow->addWidget(m_sendBtn);
    chatLayout->addLayout(inputRow);
    // Wrap chat pane in a stacked widget — index 0 = chat, index 1 = voice room
    m_chatStack = new QStackedWidget;
    m_chatStack->addWidget(chatPane);   // index 0
    buildVoiceRoomPane();               // adds index 1 (m_voiceRoomPane)
    m_chatStack->addWidget(m_voiceRoomPane);
    m_splitter->addWidget(m_chatStack);

    // 4. Member list
    auto* memberPane   = new QWidget;
    auto* memberLayout = new QVBoxLayout(memberPane);
    memberLayout->setContentsMargins(4, 4, 4, 4);
    memberLayout->addWidget(new QLabel("<b>Members</b>"));
    m_memberList = new QListWidget;
    m_memberList->setItemDelegate(new MemberDelegate(m_memberList));
    m_memberList->setIconSize(QSize(34, 34));
    memberLayout->addWidget(m_memberList);
    m_splitter->addWidget(memberPane);

    m_splitter->setSizes({165, 185, 650, 165});
    m_splitter->setStretchFactor(2, 1);

    // ── Slash autocomplete ────────────────────────────────────────────────────
    m_completerModel = new QStringListModel(this);
    m_completer = new QCompleter(m_completerModel, this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_messageInput->setCompleter(m_completer);

    // ── Signals ───────────────────────────────────────────────────────────────
    connect(m_guildList,   &QListWidget::currentRowChanged,
            this, &MainWindow::onGuildSelected);
    connect(m_dmList,      &QListWidget::currentRowChanged,
            this, &MainWindow::onDmSelected);
    connect(m_channelList, &QListWidget::currentRowChanged,
            this, &MainWindow::onChannelSelected);
    connect(m_sendBtn,       &QPushButton::clicked,     this, &MainWindow::onSendMessage);
    connect(m_messageInput,  &QLineEdit::returnPressed, this, &MainWindow::onSendMessage);
    connect(m_addChannelBtn, &QPushButton::clicked,     this, &MainWindow::onCreateChannelClicked);
    connect(m_messageInput,  &QLineEdit::textChanged,   this, &MainWindow::onInputChanged);
    connect(m_replyCancelBtn, &QPushButton::clicked, this, [this] {
        m_replyToMsgId = 0;
        m_replyToAuthor.clear();
        m_replyToContent.clear();
        m_replyBar->hide();
    });
    connect(m_channelList,   &QListWidget::customContextMenuRequested,
            this, &MainWindow::onChannelContextMenu);
    m_guildList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_guildList,   &QListWidget::customContextMenuRequested,
            this, &MainWindow::onGuildContextMenu);
    m_memberList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_memberList,  &QListWidget::customContextMenuRequested,
            this, &MainWindow::onMemberContextMenu);
    m_dmList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_dmList,      &QListWidget::customContextMenuRequested,
            this, &MainWindow::onDmContextMenu);
}

void MainWindow::buildVoiceBar()
{
    m_voiceBar = new QWidget;
    m_voiceBar->setVisible(false);
    m_voiceBar->setStyleSheet(
        "background:#1a7a4a;"
        "border-top:1px solid #155e39;"
    );
    auto* lay = new QHBoxLayout(m_voiceBar);
    lay->setContentsMargins(8, 4, 8, 4);
    lay->setSpacing(4);

    auto* dot = new QLabel(QString::fromUtf8("\xF0\x9F\x9F\xA2"));  // green circle emoji
    dot->setFixedWidth(18);
    lay->addWidget(dot);

    m_voiceBarLabel = new QLabel("Voice Connected");
    m_voiceBarLabel->setStyleSheet("color:#fff;font-size:12px;font-weight:bold;");
    lay->addWidget(m_voiceBarLabel, 1);

    m_voiceLeaveBtn = new QPushButton("\u2715");
    m_voiceLeaveBtn->setFixedSize(22, 22);
    m_voiceLeaveBtn->setToolTip("Disconnect from voice");
    m_voiceLeaveBtn->setStyleSheet(
        "QPushButton{border:none;color:#ccc;font-weight:bold;border-radius:3px;}"
        "QPushButton:hover{background:#c03030;color:#fff;}"
    );
    connect(m_voiceLeaveBtn, &QPushButton::clicked, this, &MainWindow::onLeaveVoiceClicked);
    lay->addWidget(m_voiceLeaveBtn);
}

// ── Voice room pane ───────────────────────────────────────────────────────────

void MainWindow::buildVoiceRoomPane()
{
    m_voiceRoomPane = new QWidget;
    m_voiceRoomPane->setStyleSheet("background:#313338;");

    auto* rootLayout = new QVBoxLayout(m_voiceRoomPane);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ── Header ────────────────────────────────────────────────────────────────
    auto* header = new QWidget;
    header->setStyleSheet("background:#2b2d31;border-bottom:1px solid #1e1f22;");
    header->setFixedHeight(48);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 0, 16, 0);

    m_voiceRoomChannelLabel = new QLabel(QString::fromUtf8("\xF0\x9F\x94\x8A") + " General");  // 🔊
    m_voiceRoomChannelLabel->setStyleSheet("color:#f2f3f5;font-size:16px;font-weight:bold;");
    headerLayout->addWidget(m_voiceRoomChannelLabel, 1);
    rootLayout->addWidget(header);

    // ── Participant grid (scrollable) ─────────────────────────────────────────
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea{background:transparent;border:none;}");

    m_participantContainer = new QWidget;
    m_participantContainer->setStyleSheet("background:transparent;");
    m_participantGrid = new QGridLayout(m_participantContainer);
    m_participantGrid->setContentsMargins(24, 24, 24, 24);
    m_participantGrid->setSpacing(16);
    m_participantGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    scroll->setWidget(m_participantContainer);
    rootLayout->addWidget(scroll, 1);

    // ── Controls bar (mic / deafen / disconnect) ──────────────────────────────
    auto* controlBar = new QWidget;
    controlBar->setFixedHeight(56);
    controlBar->setStyleSheet(
        "background:#232428;"
        "border-top:1px solid #1e1f22;");
    auto* ctrlLayout = new QHBoxLayout(controlBar);
    ctrlLayout->setContentsMargins(16, 8, 16, 8);
    ctrlLayout->setSpacing(8);

    // Voice status label
    auto* vcStatusLabel = new QLabel(QString::fromUtf8("\xF0\x9F\x9F\xA2") + " Voice Connected");  // 🟢
    vcStatusLabel->setStyleSheet("color:#23a55a;font-size:12px;font-weight:bold;");
    ctrlLayout->addWidget(vcStatusLabel, 1);

    // Mute button
    m_voiceMuteBtn = new QPushButton(QString::fromUtf8("\xF0\x9F\x8E\x99"));  // 🎙
    m_voiceMuteBtn->setFixedSize(36, 36);
    m_voiceMuteBtn->setCheckable(true);
    m_voiceMuteBtn->setToolTip("Mute/Unmute");
    m_voiceMuteBtn->setStyleSheet(
        "QPushButton{border:none;font-size:18px;border-radius:6px;background:#36393f;}"
        "QPushButton:hover{background:#4f545c;}"
        "QPushButton:checked{background:#ed4337;}"
    );
    connect(m_voiceMuteBtn, &QPushButton::toggled, this, [this](bool checked) {
        m_voiceMuted = checked;
        m_voiceMuteBtn->setToolTip(checked ? "Unmute" : "Mute");
        if (m_voiceEngine) m_voiceEngine->setMuted(checked);
    });
    ctrlLayout->addWidget(m_voiceMuteBtn);

    // Deafen button
    m_voiceDeafenBtn = new QPushButton(QString::fromUtf8("\xF0\x9F\x8E\xA7"));  // 🎧
    m_voiceDeafenBtn->setFixedSize(36, 36);
    m_voiceDeafenBtn->setCheckable(true);
    m_voiceDeafenBtn->setToolTip("Deafen/Undeafen");
    m_voiceDeafenBtn->setStyleSheet(
        "QPushButton{border:none;font-size:18px;border-radius:6px;background:#36393f;}"
        "QPushButton:hover{background:#4f545c;}"
        "QPushButton:checked{background:#ed4337;}"
    );
    connect(m_voiceDeafenBtn, &QPushButton::toggled, this, [this](bool checked) {
        m_voiceDeafened = checked;
        m_voiceDeafenBtn->setToolTip(checked ? "Undeafen" : "Deafen");
        if (m_voiceEngine) m_voiceEngine->setDeafened(checked);
    });
    ctrlLayout->addWidget(m_voiceDeafenBtn);

    // Disconnect button
    auto* disconnectBtn = new QPushButton("Disconnect");
    disconnectBtn->setFixedHeight(34);
    disconnectBtn->setStyleSheet(
        "QPushButton{border:none;color:#ed4337;font-size:13px;font-weight:bold;"
        "background:#2b2d31;border-radius:6px;padding:0 12px;}"
        "QPushButton:hover{background:#ed4337;color:#fff;}"
    );
    connect(disconnectBtn, &QPushButton::clicked, this, &MainWindow::onLeaveVoiceClicked);
    ctrlLayout->addWidget(disconnectBtn);

    rootLayout->addWidget(controlBar);
}

void MainWindow::refreshVoiceRoomTiles()
{
    // Clear tile tracking (widgets are deleted via deleteLater below)
    m_voiceTiles.clear();

    while (m_participantGrid->count() > 0) {
        QLayoutItem* item = m_participantGrid->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    m_voiceRoomChannelLabel->setText(
        QString::fromUtf8("\xF0\x9F\x94\x8A ") + [this]() -> QString {
            for (const auto& c : m_channels)
                if (c.id == m_currentVoiceChannelId) return c.name;
            return "Voice";
        }());

    const auto& ps = m_voiceParticipants.value(m_currentVoiceChannelId);

    if (ps.isEmpty()) {
        auto* ph = new QLabel("No one else is here yet.\nJoin from any device to start talking.");
        ph->setAlignment(Qt::AlignCenter);
        ph->setStyleSheet("color:#72767d;font-size:14px;");
        m_participantGrid->addWidget(ph, 0, 0, Qt::AlignCenter);
        return;
    }

    const int cols = qMin(4, ps.size());
    for (int i = 0; i < ps.size(); ++i) {
        const auto& p = ps[i];

        // Tile frame
        auto* tile = new QFrame;
        tile->setFixedSize(140, 170);
        tile->setStyleSheet(
            "QFrame{background:#2b2d31;border-radius:10px;border:2px solid transparent;}");

        auto* tileLayout = new QVBoxLayout(tile);
        tileLayout->setContentsMargins(0, 16, 0, 12);
        tileLayout->setSpacing(10);
        tileLayout->setAlignment(Qt::AlignCenter);

        // Avatar widget (88×88 — includes 4px ring margin)
        auto* ava = new VoiceAvatarWidget(p.username, QColor(colorForId(p.userId)), tile);
        ava->setSpeaking(m_voiceSpeakingState.value(p.userId, false));
        m_voiceTiles[p.userId] = ava;
        tileLayout->addWidget(ava, 0, Qt::AlignCenter);

        // Username label
        auto* nameLabel = new QLabel(p.username);
        nameLabel->setAlignment(Qt::AlignCenter);
        nameLabel->setStyleSheet("color:#f2f3f5;font-size:13px;font-weight:bold;background:transparent;");
        nameLabel->setMaximumWidth(128);
        nameLabel->setWordWrap(true);
        tileLayout->addWidget(nameLabel);

        m_participantGrid->addWidget(tile, i / cols, i % cols, Qt::AlignCenter);

        // Fetch server profile picture asynchronously
        const int uid = p.userId;
        m_api->getMemberAvatar(m_currentGuildId, uid,
            [this, uid](bool ok, const QByteArray& data) {
                if (!ok || data.isEmpty()) return;
                QPixmap src;
                if (!src.loadFromData(data)) return;

                // Scale and clip to VoiceAvatarWidget::kInner circle
                constexpr int sz = VoiceAvatarWidget::kInner;
                QPixmap scaled = src.scaled(sz, sz,
                    Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                const int ox = (scaled.width()  - sz) / 2;
                const int oy = (scaled.height() - sz) / 2;
                QPixmap pix(sz, sz);
                pix.fill(Qt::transparent);
                QPainter painter(&pix);
                painter.setRenderHint(QPainter::Antialiasing);
                QPainterPath path;
                path.addEllipse(0, 0, sz, sz);
                painter.setClipPath(path);
                painter.drawPixmap(0, 0, scaled, ox, oy, sz, sz);

                if (auto* w = m_voiceTiles.value(uid))
                    static_cast<VoiceAvatarWidget*>(w)->setAvatar(pix);
            });
    }
}

// ── Data loading ──────────────────────────────────────────────────────────────

void MainWindow::loadGuilds()
{
    m_api->getGuilds([this](bool ok, const QString& err, const QList<GuildInfo>& guilds) {
        if (!ok) { statusBar()->showMessage("Failed to load servers: " + err); return; }
        m_guilds = guilds;
        m_guildList->clear();
        for (int i = 0; i < m_guilds.size(); ++i) {
            const auto& g = m_guilds[i];
            auto* item = new QListWidgetItem(g.name);
            m_guildList->addItem(item);
            const int idx = i; // capture by value
            fetchGuildIconThen(g.id, g.hasIcon, [this, idx](const QIcon& icon) {
                if (idx < m_guildList->count())
                    m_guildList->item(idx)->setIcon(icon);
            });
        }
    });
}

void MainWindow::loadChannels(int guildId)
{
    m_api->getChannels(guildId,
        [this](bool ok, const QString& err, const QList<ChannelInfo>& chans) {
            if (!ok) { statusBar()->showMessage("Failed to load channels: " + err); return; }
            m_channels = chans;
            m_channelList->clear();
            for (int i = 0; i < m_channels.size(); ++i) {
                const auto& c = m_channels[i];
                m_channelGuildMap[c.id] = c.guildId;   // persist for cross-guild unread
                m_channelList->addItem(channelItemText(c));
                // Fetch voice state for voice channels
                if (c.type == QStringLiteral("voice")) {
                    m_api->getVoiceState(c.id,
                        [this, channelId = c.id](bool ok2, const QString&,
                                                  const QList<VoiceParticipant>& ps) {
                            if (ok2) {
                                m_voiceParticipants[channelId] = ps;
                                updateVoiceChannelItem(channelId);
                            }
                        });
                } else {
                    const int cnt  = m_unreadChannels.value(c.id, 0);
                    const bool isMention = m_mentionChannels.contains(c.id);
                    if (cnt > 0) {
                        auto* item = m_channelList->item(i);
                        QFont f = item->font(); f.setBold(true); item->setFont(f);
                        item->setForeground(isMention ? QColor(0xED, 0x4C, 0x5E)
                                                      : QColor(0xDC, 0xDD, 0xDE));
                        item->setText(channelItemText(c) + QStringLiteral(" (%1)").arg(cnt));
                    }
                }
            }
            for (int i = 0; i < m_channels.size(); ++i) {
                if (m_channels[i].name == "general"
                    && m_channels[i].type != QStringLiteral("voice")) {
                    m_channelList->setCurrentRow(i);
                    break;
                }
            }
        });
}

QString MainWindow::channelItemText(const ChannelInfo& c) const
{
    if (c.type == QStringLiteral("voice")) {
        const auto& ps = m_voiceParticipants.value(c.id);
        QString text = QString::fromUtf8("\xF0\x9F\x94\x8A ") + c.name;  // 🔊
        if (!ps.isEmpty()) {
            QStringList names;
            for (const auto& p : ps) names << p.username;
            text += QStringLiteral("  (%1)").arg(names.join(", "));
        }
        return text;
    }
    return QStringLiteral("# ") + c.name;
}

void MainWindow::updateVoiceChannelItem(int channelId)
{
    for (int i = 0; i < m_channels.size(); ++i) {
        if (m_channels[i].id == channelId) {
            if (auto* item = m_channelList->item(i))
                item->setText(channelItemText(m_channels[i]));
            return;
        }
    }
}

void MainWindow::loadVoiceParticipants(int channelId)
{
    m_api->getVoiceState(channelId,
        [this, channelId](bool ok, const QString&, const QList<VoiceParticipant>& ps) {
            if (ok) {
                m_voiceParticipants[channelId] = ps;
                updateVoiceChannelItem(channelId);
                if (m_chatStack->currentIndex() == 1 && channelId == m_currentVoiceChannelId)
                    refreshVoiceRoomTiles();
            }
        });
}

void MainWindow::loadMessages(int channelId)
{
    m_messageView->clear();
    m_api->getMessages(channelId, 50,
        [this](bool ok, const QString& err, const QList<MessageInfo>& msgs) {
            if (!ok) { statusBar()->showMessage("Failed: " + err); return; }
            m_messageView->clear();
            if (msgs.isEmpty()) return;

            // Collect unique author IDs so we pre-fetch every avatar once,
            // then render ALL messages in order when the last fetch completes.
            QSet<int> uniqueAuthors;
            for (const auto& msg : msgs)
                uniqueAuthors.insert(msg.authorId);

            const int gid = m_currentGuildId;
            auto remaining = std::make_shared<int>(uniqueAuthors.size());
            auto avatars   = std::make_shared<QHash<int, QString>>();

            for (int uid : uniqueAuthors) {
                fetchMemberAvatarThen(gid, uid,
                    [this, remaining, avatars, msgs, uid](const QString& cell) {
                        (*avatars)[uid] = cell;
                        if (--(*remaining) != 0) return;
                        // All avatars ready — render in original order
                        for (const auto& msg : msgs) {
                            QDateTime dt = QDateTime::fromString(msg.createdAt, Qt::ISODate);
                            if (!dt.isValid()) dt = QDateTime::fromString(msg.createdAt, Qt::ISODateWithMs);
                            const QString ts = dt.isValid() ? dt.toLocalTime().toString("hh:mm") : msg.createdAt.left(16);
                            QString displayName = msg.authorUsername;
                            QString roleColor, roleName;
                            for (const auto& m : m_members) {
                                if (m.userId == msg.authorId) {
                                    if (!m.nickname.isEmpty())         displayName = m.nickname;
                                    else if (!m.displayName.isEmpty()) displayName = m.displayName;
                                    if (m.role == "owner") {
                                        roleName = "Server Owner"; roleColor = "#FAA61A";
                                    } else {
                                        const RoleInfo* top = nullptr;
                                        for (const auto& r : m.roles)
                                            if (!top || r.position > top->position) top = &r;
                                        if (top) { roleName = top->name; roleColor = top->color; }
                                    }
                                    break;
                                }
                            }
                            appendChatLine(displayName, msg.authorId, msg.content, ts,
                                           msg.authorIsBot, (*avatars)[msg.authorId], roleColor, roleName,
                                           msg.id, msg.replyToAuthor, msg.replyToContent);
                        }
                        m_messageView->verticalScrollBar()->setValue(
                            m_messageView->verticalScrollBar()->maximum());
                    });
            }
        });
}

void MainWindow::loadMembers(int guildId)
{
    m_api->getMembers(guildId,
        [this, guildId](bool ok, const QString&, const QList<MemberInfo>& members) {
            if (!ok) return;
            m_members = members;
            m_memberList->clear();
            for (int i = 0; i < m_members.size(); ++i) {
                const auto& m = m_members[i];

                // Display name: nickname > displayName > username
                QString label = m.nickname.isEmpty()
                    ? (m.displayName.isEmpty() ? m.username : m.displayName)
                    : m.nickname;
                if (m.isBot) label += " \U0001F916";

                // Top role: highest position in assigned roles
                QString roleColor, roleName;
                if (m.role == "owner") {
                    roleName  = "Server Owner";
                    roleColor = "#FAA61A";
                } else {
                    const RoleInfo* top = nullptr;
                    for (const auto& r : m.roles)
                        if (!top || r.position > top->position) top = &r;
                    if (top) { roleName = top->name; roleColor = top->color; }
                }

                auto* item = new QListWidgetItem(label);
                item->setData(RoleNameRole,  roleName);
                item->setData(RoleColorRole, roleColor);
                // Status dot: online=bright, offline=faded (Discord-style)
                const QString dotStatus = m.isOnline ? m.status : "offline";
                item->setData(kStatusDotRole, dotStatus);
                item->setForeground(m.isOnline ? QColor(0xDC,0xDD,0xDE)
                                               : QColor(0x4F,0x54,0x5C));
                m_memberList->addItem(item);

                // Async: fetch server avatar and set as icon
                const int idx = i;
                fetchMemberIconThen(guildId, m.userId,
                    [this, idx](const QIcon& icon) {
                        if (idx < m_memberList->count())
                            m_memberList->item(idx)->setIcon(icon);
                    });
            }
        });
}

void MainWindow::loadDms()
{
    m_api->getDms([this](bool ok, const QString&, const QList<DmConversationInfo>& dms) {
        if (!ok) return;
        m_dms = dms;
        m_dmList->clear();
        for (const auto& d : m_dms) {
            const QString name = d.otherDisplayName.isEmpty()
                                 ? d.otherUsername : d.otherDisplayName;
            const int cnt = m_unreadDms.value(d.id, 0);
            QString text = QStringLiteral("@ ") + name;
            if (cnt > 0) text += QStringLiteral(" (%1)").arg(cnt);
            auto* item = new QListWidgetItem(text);
            if (cnt > 0) { QFont f = item->font(); f.setBold(true); item->setFont(f); }
            m_dmList->addItem(item);
        }
    });
}

void MainWindow::loadDmMessages(int dmId)
{
    m_messageView->clear();
    m_api->getDmMessages(dmId, 50,
        [this](bool ok, const QString& err, const QList<DmMessageInfo>& msgs) {
            if (!ok) { statusBar()->showMessage("Failed: " + err); return; }
            m_messageView->clear();
            if (msgs.isEmpty()) return;

            // Collect unique users; for own messages prefer m_me.hasAvatar
            // (covers the case where the user set an avatar after old messages
            // were sent and the per-message flag might be stale).
            QHash<int, bool> toFetch;
            for (const auto& msg : msgs) {
                if (!toFetch.contains(msg.authorId)) {
                    bool hasAv = msg.authorHasAvatar;
                    if (msg.authorId == m_me.id) hasAv = hasAv || m_me.hasAvatar;
                    toFetch[msg.authorId] = hasAv;
                }
            }

            auto remaining = std::make_shared<int>(toFetch.size());
            auto avatars   = std::make_shared<QHash<int, QString>>();

            for (auto it = toFetch.begin(); it != toFetch.end(); ++it) {
                int  uid   = it.key();
                bool hasAv = it.value();
                fetchAvatarThen(uid, hasAv,
                    [this, remaining, avatars, msgs, uid](const QString& cell) {
                        (*avatars)[uid] = cell;
                        if (--(*remaining) != 0) return;
                        // All avatars ready — render in original order
                        for (const auto& m : msgs) {
                            QDateTime dt = QDateTime::fromString(m.createdAt, Qt::ISODate);
                            if (!dt.isValid()) dt = QDateTime::fromString(m.createdAt, Qt::ISODateWithMs);
                            const QString ts = dt.isValid() ? dt.toLocalTime().toString("hh:mm") : m.createdAt.left(16);
                            appendChatLine(m.authorUsername, m.authorId, m.content, ts,
                                           m.authorIsBot, (*avatars)[m.authorId], {}, {},
                                           m.id, m.replyToAuthor, m.replyToContent);
                        }
                        m_messageView->verticalScrollBar()->setValue(
                            m_messageView->verticalScrollBar()->maximum());
                    });
            }
        });
}

void MainWindow::updateCommands()
{
    if (m_currentGuildId == 0 || m_inDmMode) {
        m_commands.clear();
        m_completerModel->setStringList({});
        return;
    }
    m_api->getCommands(m_currentGuildId,
        [this](bool ok, const QString&, const QList<CommandInfo>& cmds) {
            if (!ok) return;
            m_commands = cmds;
            QStringList names;
            for (const auto& c : m_commands)
                names << QStringLiteral("/%1  — %2").arg(c.name, c.description);
            m_completerModel->setStringList(names);
        });
}

// ── Slot handlers ─────────────────────────────────────────────────────────────

void MainWindow::onGuildSelected(int row)
{
    if (row < 0 || row >= m_guilds.size()) return;
    m_inDmMode          = false;
    m_currentDmId       = 0;
    m_currentChannelId  = 0;
    m_dmList->setCurrentRow(-1);

    const GuildInfo& g   = m_guilds[row];
    m_currentGuildId     = g.id;
    m_unreadGuilds.remove(g.id);
    if (auto* item = m_guildList->item(row))
        item->setForeground(QApplication::palette().text());
    m_guildNameLabel->setText(QStringLiteral("<b>%1</b>").arg(g.name.toHtmlEscaped()));
    m_addChannelBtn->setEnabled(true);
    m_messageView->clear();
    m_messageInput->setEnabled(false);
    m_sendBtn->setEnabled(false);

    loadChannels(g.id);
    loadMembers(g.id);
    updateCommands();
}

void MainWindow::onDmSelected(int row)
{
    if (row < 0 || row >= m_dms.size()) return;
    m_inDmMode         = true;
    m_currentChannelId = 0;
    m_currentGuildId   = 0;
    m_guildList->setCurrentRow(-1);
    m_channelList->setCurrentRow(-1);

    const DmConversationInfo& dm = m_dms[row];
    m_currentDmId = dm.id;

    // Clear unread badge for this DM
    m_unreadDms.remove(dm.id);
    const QString otherName = dm.otherDisplayName.isEmpty()
                              ? dm.otherUsername : dm.otherDisplayName;
    if (auto* item = m_dmList->item(row)) {
        item->setText("@ " + otherName);
        QFont f = item->font(); f.setBold(false); item->setFont(f);
    }

    m_guildNameLabel->setText(
        QStringLiteral("<b>DM with @%1</b>").arg(otherName.toHtmlEscaped()));
    m_addChannelBtn->setEnabled(false);
    m_channelList->clear();
    m_memberList->clear();
    m_messageInput->setEnabled(true);
    m_sendBtn->setEnabled(true);
    m_messageInput->setPlaceholderText(
        QStringLiteral("Message @%1").arg(otherName));

    loadDmMessages(dm.id);
    m_completerModel->setStringList({});
}

void MainWindow::onChannelSelected(int row)
{
    if (row < 0 || row >= m_channels.size()) return;
    const ChannelInfo& ch = m_channels[row];
    m_inDmMode = false;

    if (ch.type == QStringLiteral("voice")) {
        // Join or leave voice channel
        if (m_currentVoiceChannelId == ch.id) {
            // Already in this channel — just make sure the room view is visible
            m_chatStack->setCurrentIndex(1);
            return;
        }
        // Join voice
        m_api->joinVoice(ch.id, [this, ch](bool ok, const QString& err) {
            if (!ok) {
                ForjPrompt::warning(this, "Voice", "Failed to join voice: " + err);
                return;
            }
            m_currentVoiceChannelId = ch.id;
            m_voiceBarLabel->setText(QStringLiteral("Voice: %1").arg(ch.name));
            m_voiceBar->setVisible(true);

            // Start audio engine
            if (!m_voiceEngine) {
                m_voiceEngine = new VoiceEngine(this);
                connect(m_voiceEngine, &VoiceEngine::audioFrame, this,
                    [this](const QByteArray& pcm) {
                        m_ws->sendAudioFrame(m_currentVoiceChannelId, pcm);
                    });
                connect(m_voiceEngine, &VoiceEngine::speakingChanged, this,
                    [this](bool speaking) {
                        // Update own tile ring immediately
                        m_voiceSpeakingState[m_me.id] = speaking;
                        if (auto* w = m_voiceTiles.value(m_me.id))
                            static_cast<VoiceAvatarWidget*>(w)->setSpeaking(speaking);
                        // Notify others
                        m_ws->sendVoiceSpeaking(m_currentVoiceChannelId, speaking);
                    });
                m_voiceEngine->start();  // uses default devices (index 0)
            }

            // Switch chat area to voice room view
            m_chatStack->setCurrentIndex(1);
            loadVoiceParticipants(ch.id);
        });
        return;
    }

    // Text channel — switch to chat view
    m_chatStack->setCurrentIndex(0);
    m_currentChannelId = ch.id;

    // Clear unread badge for this channel
    m_unreadChannels.remove(ch.id);
    m_mentionChannels.remove(ch.id);
    if (auto* item = m_channelList->item(row)) {
        item->setText(channelItemText(ch));
        QFont f = item->font(); f.setBold(false); item->setFont(f);
        item->setForeground(QApplication::palette().text());
    }

    m_messageInput->setEnabled(true);
    m_sendBtn->setEnabled(true);
    m_messageInput->setPlaceholderText("Message\u2026  (type / for commands)");
    m_messageInput->setFocus();
    loadMessages(m_currentChannelId);
}

void MainWindow::onSendMessage()
{
    const QString text = m_messageInput->text().trimmed();
    if (text.isEmpty()) return;

    m_messageInput->clear();
    m_messageInput->setEnabled(false);

    // Capture and clear reply state before the async round-trip
    const int replyId = m_replyToMsgId;
    m_replyToMsgId = 0;
    m_replyToAuthor.clear();
    m_replyToContent.clear();
    m_replyBar->hide();

    auto reenable = [this] {
        m_messageInput->setEnabled(true);
        m_messageInput->setFocus();
    };

    // DM mode
    if (m_inDmMode && m_currentDmId != 0) {
        m_api->sendDm(m_currentDmId, text,
            [this, reenable](bool ok, const QString& err, const DmMessageInfo& msg) {
                reenable();
                if (ok) {
                    appendDmMessage(msg);
                    m_messageView->verticalScrollBar()->setValue(
                        m_messageView->verticalScrollBar()->maximum());
                } else {
                    statusBar()->showMessage("Send failed: " + err, 4000);
                }
            }, replyId);
        return;
    }

    if (m_currentChannelId == 0) { reenable(); return; }

    // Slash command
    if (text.startsWith('/')) {
        const QString body    = text.mid(1);
        const int     space   = body.indexOf(' ');
        QString       cmdName = (space == -1) ? body : body.left(space);
        QString       args    = (space == -1) ? QString{} : body.mid(space + 1);

        // Strip autocomplete description suffix if user picked one
        if (int dash = cmdName.indexOf("  —"); dash != -1)
            cmdName = cmdName.left(dash);

        m_api->invokeCommand(m_currentChannelId, cmdName.toLower().trimmed(), args,
            [this, reenable](bool ok, const QString& err, const MessageInfo&) {
                reenable();
                if (!ok) statusBar()->showMessage("Command error: " + err, 4000);
            });
        return;
    }

    // Regular message
    m_api->sendMessage(m_currentChannelId, text,
        [this, reenable](bool ok, const QString& err, const MessageInfo& msg) {
            reenable();
            if (ok) {
                // Show immediately — don't wait for WS echo
                appendGuildMessage(msg);
                m_messageView->verticalScrollBar()->setValue(
                    m_messageView->verticalScrollBar()->maximum());
            } else {
                statusBar()->showMessage("Failed to send: " + err, 4000);
            }
        }, replyId);
}

void MainWindow::onCreateGuildClicked()
{
    const QString name = ForjPrompt::getText(this, "New Server", "Server name:");
    if (name.trimmed().isEmpty()) return;
    m_api->createGuild(name.trimmed(),
        [this](bool ok, const QString& err, const GuildInfo& guild) {
            if (ok) {
                m_guilds.append(guild);
                m_guildList->addItem(guild.name);
                m_guildList->setCurrentRow(m_guilds.size() - 1);
            } else {
                ForjPrompt::warning(this, "Error", err);
            }
        });
}

void MainWindow::onJoinGuildClicked()
{
    const QString code = ForjPrompt::getText(this, "Join Server", "Enter invite code:");
    if (code.trimmed().isEmpty()) return;
    m_api->joinGuild(code.trimmed(),
        [this](bool ok, const QString& err, const GuildInfo& guild) {
            if (ok) {
                m_guilds.append(guild);
                m_guildList->addItem(guild.name);
                m_guildList->setCurrentRow(m_guilds.size() - 1);
            } else {
                ForjPrompt::warning(this, "Error", err);
            }
        });
}

void MainWindow::onCreateChannelClicked()
{
    if (m_currentGuildId == 0) return;
    const QStringList types = {"Text Channel", "Voice Channel"};
    const QString typeChoice = ForjPrompt::getItem(this, "Channel Type",
                                                    "Select channel type:", types, 0);
    if (typeChoice.isEmpty()) return;
    const QString channelType = (typeChoice == "Voice Channel") ? "voice" : "text";

    const QString name = ForjPrompt::getText(this, "New Channel",
                                              channelType == "voice"
                                              ? "Voice channel name (lowercase, no spaces):"
                                              : "Channel name (lowercase, no spaces):");
    if (name.trimmed().isEmpty()) return;
    m_api->createChannel(m_currentGuildId,
        name.trimmed().toLower().replace(' ', '-'),
        [this, channelType](bool ok, const QString& err, const ChannelInfo& ch) {
            if (ok) {
                m_channels.append(ch);
                m_channelList->addItem(channelItemText(ch));
            } else {
                ForjPrompt::warning(this, "Error", err);
            }
        }, channelType);
}

void MainWindow::onOpenDmClicked()
{
    const QString username = ForjPrompt::getText(this, "New DM", "Username to DM:");
    if (username.trimmed().isEmpty()) return;

    m_api->searchUsers(username.trimmed(),
        [this](bool ok, const QString&, const QList<UserInfo>& users) {
            if (!ok || users.isEmpty()) {
                ForjPrompt::information(this, "Not found", "No users found.");
                return;
            }
            if (users.size() == 1) {
                openDmWithUser(users[0]);
                return;
            }
            QStringList names;
            for (const auto& u : users) names << u.username;
            const QString choice = ForjPrompt::getItem(
                this, "Choose user", "Select:", names, 0);
            if (choice.isEmpty()) return;
            for (const auto& u : users) {
                if (u.username == choice) { openDmWithUser(u); return; }
            }
        });
}

void MainWindow::onNotifyClicked()
{
    m_unreadNotifications = 0;
    if (m_notifyAction)
        m_notifyAction->setText(QStringLiteral("\U0001F514"));

    auto* dlg = new ForjDialog("Notifications", this);
    dlg->resize(420, 510);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    auto* lay  = new QVBoxLayout(dlg->body());
    lay->setContentsMargins(8, 8, 8, 8);
    auto* list = new QListWidget(dlg);
    list->setWordWrap(true);
    list->setSpacing(2);

    if (m_notifications.isEmpty()) {
        list->addItem("No notifications yet.");
    } else {
        for (int i = m_notifications.size() - 1; i >= 0; --i) {
            const auto& n = m_notifications[i];
            const QString icon = (n.type == NotificationItem::Type::DM) ? "\U0001F4AC " :
                                 (n.type == NotificationItem::Type::FriendRequest) ? "\U0001F465 " :
                                 "\U0001F514 ";
            auto* item = new QListWidgetItem(
                icon + n.title + "  \u2022  " + n.timestamp + "\n" + n.body);
            item->setData(Qt::UserRole,     n.channelId);
            item->setData(Qt::UserRole + 1, n.dmId);
            item->setData(Qt::UserRole + 2, n.guildId);
            list->addItem(item);
        }
    }
    lay->addWidget(list, 1);

    auto* btnRow  = new QHBoxLayout;
    auto* clearBtn = new QPushButton("Clear All");
    connect(clearBtn, &QPushButton::clicked, this, [this, dlg] {
        m_notifications.clear();
        dlg->close();
    });
    btnRow->addStretch();
    btnRow->addWidget(clearBtn);
    lay->addLayout(btnRow);

    // Navigate to channel/DM on double-click
    connect(list, &QListWidget::itemDoubleClicked, this, [this, dlg](QListWidgetItem* item) {
        const int chId  = item->data(Qt::UserRole).toInt();
        const int dmId  = item->data(Qt::UserRole + 1).toInt();
        const int gId   = item->data(Qt::UserRole + 2).toInt();
        dlg->close();

        if (chId > 0) {
            // Switch to the right guild first
            for (int gi = 0; gi < m_guilds.size(); ++gi) {
                if (m_guilds[gi].id == gId ||
                    m_channelGuildMap.value(chId, 0) == m_guilds[gi].id)
                {
                    m_guildList->setCurrentRow(gi);
                    // After loadChannels fires, find and select the channel
                    for (int ci = 0; ci < m_channels.size(); ++ci) {
                        if (m_channels[ci].id == chId) {
                            m_channelList->setCurrentRow(ci);
                            break;
                        }
                    }
                    break;
                }
            }
        } else if (dmId > 0) {
            for (int di = 0; di < m_dms.size(); ++di) {
                if (m_dms[di].id == dmId) {
                    m_dmList->setCurrentRow(di);
                    break;
                }
            }
        }
    });

    dlg->exec();
}

void MainWindow::onFriendsClicked()
{
    if (m_friendsDialog) {
        m_friendsDialog->raise();
        m_friendsDialog->activateWindow();
        return;
    }
    m_friendsDialog = new FriendsDialog(m_api, m_me, this);
    connect(m_friendsDialog, &FriendsDialog::openDmRequested, this, [this](int userId, const QString& username) {
        // Look for an existing DM
        for (int i = 0; i < m_dms.size(); ++i) {
            const auto& dm = m_dms[i];
            if (dm.otherUserId == userId) {
                m_guildList->clearSelection();
                m_dmList->setCurrentRow(i);
                return;
            }
        }
        // Otherwise open a new DM via API
        m_api->openDm(userId, [this](bool ok, const QString& err, const DmConversationInfo& dm) {
            if (!ok) { qWarning() << "DM open failed:" << err; return; }
            loadDms();
            // After reload, select it
            for (int i = 0; i < m_dms.size(); ++i) {
                if (m_dms[i].id == dm.id) { m_dmList->setCurrentRow(i); break; }
            }
        });
    });
    connect(m_friendsDialog, &FriendsDialog::inviteToGuildRequested,
            this, &MainWindow::onInviteToGuildRequested);
    connect(m_friendsDialog, &QObject::destroyed, this, [this] { m_friendsDialog = nullptr; });
    m_friendsDialog->show();
}

void MainWindow::onFriendRequest(int fromId, const QString& fromUsername, const QString& fromDisplayName)
{
    const QString display = fromDisplayName.isEmpty() ? fromUsername
                            : QStringLiteral("%1 (%2)").arg(fromDisplayName, fromUsername);
    NotificationItem n;
    n.type        = NotificationItem::Type::FriendRequest;
    n.title       = "Friend Request";
    n.body        = QStringLiteral("%1 sent you a friend request").arg(display);
    n.timestamp   = QDateTime::currentDateTime().toString("hh:mm");
    n.fromUserId  = fromId;
    m_notifications.append(n);
    ++m_unreadNotifications;
    if (m_notifyAction)
        m_notifyAction->setText(QStringLiteral("\U0001F514 (%1)").arg(m_unreadNotifications));

    if (m_friendsDialog)
        m_friendsDialog->notifyFriendRequest(fromId, fromUsername, fromDisplayName);

    if (m_trayIcon && m_trayIcon->isVisible())
        m_trayIcon->showMessage("Friend Request", n.body, QSystemTrayIcon::Information, 3000);
}

void MainWindow::onFriendAccepted(int byId, const QString& byUsername, const QString& byDisplayName)
{
    const QString display = byDisplayName.isEmpty() ? byUsername
                            : QStringLiteral("%1 (%2)").arg(byDisplayName, byUsername);
    NotificationItem n;
    n.type        = NotificationItem::Type::FriendRequest;
    n.title       = "Friend Accepted";
    n.body        = QStringLiteral("%1 accepted your friend request").arg(display);
    n.timestamp   = QDateTime::currentDateTime().toString("hh:mm");
    n.fromUserId  = byId;
    m_notifications.append(n);
    ++m_unreadNotifications;
    if (m_notifyAction)
        m_notifyAction->setText(QStringLiteral("\U0001F514 (%1)").arg(m_unreadNotifications));

    if (m_friendsDialog)
        m_friendsDialog->notifyFriendAccepted(byId, byUsername);

    if (m_trayIcon && m_trayIcon->isVisible())
        m_trayIcon->showMessage("Friend Accepted", n.body, QSystemTrayIcon::Information, 3000);
}

void MainWindow::onGuildInviteReceived(int guildId, const QString& guildName,
                                        const QString& inviteCode, const QString& inviterUsername)
{
    const QString msg = QStringLiteral("%1 invited you to join \"%2\"").arg(inviterUsername, guildName);
    if (!ForjPrompt::question(this, "Guild Invite", msg + "\n\nAccept and join?"))
        return;

    m_api->joinGuild(inviteCode, [this, guildId, guildName](bool ok, const QString& err, const GuildInfo&) {
        if (!ok) {
            ForjPrompt::warning(this, "Join Failed", err);
            return;
        }
        loadGuilds();
    });
}

void MainWindow::onInviteToGuildRequested(int userId, const QString& /*username*/)
{
    if (m_guilds.isEmpty()) {
        ForjPrompt::information(this, "Invite", "You are not a member of any servers.");
        return;
    }

    // Build guild picker
    QStringList names;
    for (const auto& g : m_guilds)
        names << g.name;

    const QString chosen = ForjPrompt::getItem(this, "Invite to Server",
                                                "Select a server to invite to:", names, 0);
    if (chosen.isEmpty()) return;

    int guildId = 0;
    for (const auto& g : m_guilds) {
        if (g.name == chosen) { guildId = g.id; break; }
    }
    if (!guildId) return;

    m_api->inviteUserToGuild(guildId, userId, [this](bool okRes, const QString& errRes) {
        if (!okRes)
            ForjPrompt::warning(this, "Error", errRes);
    });
}

void MainWindow::onSettingsClicked()
{
    auto* dlg = new SettingsDialog(m_api, m_me, this);
    connect(dlg, &SettingsDialog::profileUpdated, this, [this](const UserInfo& user) {
        // Invalidate caches for own user across ALL guilds
        m_avatarCache.remove(user.id);
        m_avatarPending.remove(user.id);
        m_avatarFetching.remove(user.id);
        const QString userSuffix = QStringLiteral(":%1").arg(user.id);
        for (auto it = m_memberAvatarCache.begin(); it != m_memberAvatarCache.end(); )
            it = it.key().endsWith(userSuffix) ? m_memberAvatarCache.erase(it) : ++it;
        for (auto it = m_memberIconCache.begin(); it != m_memberIconCache.end(); )
            it = it.key().endsWith(userSuffix) ? m_memberIconCache.erase(it) : ++it;
        for (auto it = m_memberAvatarFetching.begin(); it != m_memberAvatarFetching.end(); )
            it = it->endsWith(userSuffix) ? m_memberAvatarFetching.erase(it) : ++it;
        for (auto it = m_memberIconFetching.begin(); it != m_memberIconFetching.end(); )
            it = it->endsWith(userSuffix) ? m_memberIconFetching.erase(it) : ++it;
        m_me = user;
        setWindowTitle(QStringLiteral("Forj — %1").arg(user.username));
        // Reload current view so messages immediately show updated pfp
        if (m_inDmMode && m_currentDmId != 0)
            loadDmMessages(m_currentDmId);
        else if (!m_inDmMode && m_currentChannelId != 0)
            loadMessages(m_currentChannelId);
        if (m_currentGuildId != 0)
            loadMembers(m_currentGuildId);
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->exec();
}

void MainWindow::onChannelContextMenu(const QPoint& pos)
{
    int row = m_channelList->row(m_channelList->itemAt(pos));
    if (row < 0 || row >= m_channels.size()) return;
    const ChannelInfo& ch = m_channels[row];

    QMenu menu(this);

    if (ch.type == QStringLiteral("voice")) {
        if (m_currentVoiceChannelId == ch.id) {
            menu.addAction("Disconnect", this, [this]() { onLeaveVoiceClicked(); });
        } else {
            menu.addAction("Join Voice", this, [this, row]() { onChannelSelected(row); });
        }
        menu.addSeparator();
        const auto& ps = m_voiceParticipants.value(ch.id);
        if (!ps.isEmpty()) {
            auto* lbl = menu.addAction(
                QStringLiteral("In channel: %1 user(s)").arg(ps.size()));
            lbl->setEnabled(false);
        }
    } else {
        menu.addAction("Manage Webhooks", this, [this, ch]() {
            m_api->getWebhooks(ch.id,
                [this, ch](bool ok, const QString&, const QList<WebhookInfo>& hooks) {
                    QString info = QStringLiteral("Webhooks for #%1\n\n").arg(ch.name);
                    for (const auto& h : hooks)
                        info += QStringLiteral("\u2022 %1\n  POST http://209.126.5.36:8000%2\n\n")
                                    .arg(h.name, h.url);
                    if (hooks.isEmpty()) info += "(no webhooks yet)";

                    if (ForjPrompt::question(this, "Webhooks",
                            info + "\n\nCreate a new webhook?")) {
                        const QString wname = ForjPrompt::getText(this, "New Webhook",
                                                                  "Webhook name:");
                        if (!wname.trimmed().isEmpty()) {
                            m_api->createWebhook(ch.id, wname.trimmed(),
                                [this](bool ok, const QString& err, const WebhookInfo& h) {
                                    if (ok) {
                                        const QString url =
                                            QStringLiteral("http://209.126.5.36:8000%1").arg(h.url);
                                        QApplication::clipboard()->setText(url);
                                        ForjPrompt::information(this, "Webhook Created",
                                            "Webhook URL copied to clipboard:\n" + url);
                                    } else {
                                        ForjPrompt::warning(this, "Error", err);
                                    }
                                });
                        }
                    }
                });
        });
    }
    menu.exec(m_channelList->mapToGlobal(pos));
}

void MainWindow::onGuildContextMenu(const QPoint& pos)
{
    int row = m_guildList->row(m_guildList->itemAt(pos));
    if (row < 0 || row >= m_guilds.size()) return;
    const GuildInfo& g = m_guilds[row];

    // Determine current user's role in this guild
    QString myRole = "member";
    for (const auto& m : m_members) {
        if (m.userId == m_me.id) { myRole = m.role; break; }
    }
    // Reload members for this guild context to get the role
    // (We get it from the loaded members if this guild is currently selected)
    if (m_currentGuildId != g.id) {
        // Fetch synchronously via cached data isn't possible; default to member
        // The dialog will show/hide features appropriately
    }

    QMenu menu(this);
    menu.addAction("Server Settings", this, [this, g, myRole, row] {
        auto* dlg = new ServerSettingsDialog(m_api, g, m_me.id, myRole, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &QDialog::finished, this, [this, dlg, row](int) {
            // Refresh guild icon/name in list if changed
            const GuildInfo updated = dlg->updatedGuild();
            if (row < m_guilds.size()) {
                m_guilds[row] = updated;
                m_guildList->item(row)->setText(updated.name);
                // Invalidate icon cache and refetch
                m_guildIconCache.remove(updated.id);
                fetchGuildIconThen(updated.id, updated.hasIcon, [this, row](const QIcon& icon) {
                    if (row < m_guildList->count())
                        m_guildList->item(row)->setIcon(icon);
                });
            }
        });
        dlg->show();
    });

    menu.addSeparator();

    menu.addAction("Copy Invite Code", this, [this, g] {
        QApplication::clipboard()->setText(g.inviteCode);
        statusBar()->showMessage("Invite code copied!", 3000);
    });

    menu.addSeparator();

    if (g.ownerId == m_me.id) {
        menu.addAction("Delete Server", this, [this, g, row] {
            if (!ForjPrompt::question(this, "Delete Server",
                QStringLiteral("Permanently delete \"%1\"? This cannot be undone.").arg(g.name)))
                return;
            m_api->deleteGuild(g.id, [this, row](bool ok, const QString& err) {
                if (ok) {
                    m_guilds.removeAt(row);
                    delete m_guildList->takeItem(row);
                    m_currentGuildId = 0;
                    m_channelList->clear();
                    m_messageView->clear();
                    m_memberList->clear();
                } else {
                    ForjPrompt::warning(this, "Error", err);
                }
            });
        });
    } else {
        menu.addAction("Leave Server", this, [this, g, row] {
            if (!ForjPrompt::question(this, "Leave Server",
                QStringLiteral("Leave \"%1\"?").arg(g.name)))
                return;
            m_api->leaveGuild(g.id, [this, row](bool ok, const QString& err) {
                if (ok) {
                    m_guilds.removeAt(row);
                    delete m_guildList->takeItem(row);
                    m_currentGuildId = 0;
                    m_channelList->clear();
                    m_messageView->clear();
                    m_memberList->clear();
                } else {
                    ForjPrompt::warning(this, "Error", err);
                }
            });
        });
    }

    menu.exec(m_guildList->mapToGlobal(pos));
}

// ── User context menu ─────────────────────────────────────────────────────────

void MainWindow::onMemberContextMenu(const QPoint& pos)
{
    int row = m_memberList->row(m_memberList->itemAt(pos));
    if (row < 0 || row >= m_members.size()) return;
    showUserContextMenu(m_members[row].userId, m_memberList->mapToGlobal(pos));
}

void MainWindow::onDmContextMenu(const QPoint& pos)
{
    int row = m_dmList->row(m_dmList->itemAt(pos));
    if (row < 0 || row >= m_dms.size()) return;
    showUserContextMenu(m_dms[row].otherUserId, m_dmList->mapToGlobal(pos));
}

bool MainWindow::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == m_messageView->viewport() && e->type() == QEvent::ContextMenu) {
        auto* ce = static_cast<QContextMenuEvent*>(e);
        const QString anchor = m_messageView->anchorAt(ce->pos());
        if (anchor.startsWith("forj://user/")) {
            showUserContextMenu(anchor.mid(13).toInt(), ce->globalPos());
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, e);
}

void MainWindow::showUserContextMenu(int userId, const QPoint& globalPos)
{
    // Resolve name info from members list
    QString username, displayName, nickname;
    QList<RoleInfo> roles;
    QString memberRole;
    for (const auto& m : m_members) {
        if (m.userId == userId) {
            username    = m.username;
            displayName = m.displayName;
            nickname    = m.nickname;
            roles       = m.roles;
            memberRole  = m.role;
            break;
        }
    }
    // Fallback: look in DM list
    if (username.isEmpty()) {
        for (const auto& d : m_dms) {
            if (d.otherUserId == userId) {
                username    = d.otherUsername;
                displayName = d.otherDisplayName;
                break;
            }
        }
    }

    const QString shownName = !nickname.isEmpty() ? nickname
                            : !displayName.isEmpty() ? displayName
                            : username;

    QMenu menu;

    // Non-clickable header showing the display name
    QAction* header = menu.addAction(
        shownName.isEmpty() ? QStringLiteral("User #%1").arg(userId) : shownName);
    header->setEnabled(false);
    QFont hf = header->font();
    hf.setBold(true);
    header->setFont(hf);

    menu.addSeparator();

    // Send Message (open DM)
    if (userId != m_me.id) {
        menu.addAction("Send Message", this, [this, userId, username] {
            for (int i = 0; i < m_dms.size(); ++i) {
                if (m_dms[i].otherUserId == userId) {
                    m_inDmMode = true;
                    m_dmList->setCurrentRow(i);
                    onDmSelected(i);
                    return;
                }
            }
            UserInfo u; u.id = userId; u.username = username;
            openDmWithUser(u);
        });
    }

    // Mention (guild channel only)
    if (!m_inDmMode && m_currentGuildId > 0 && userId != m_me.id) {
        menu.addAction("Mention", this, [this, shownName] {
            m_messageInput->setText(m_messageInput->text() + "@" + shownName + " ");
            m_messageInput->setFocus();
        });
    }

    if (!username.isEmpty()) {
        menu.addAction("Copy Username", this, [username] {
            QApplication::clipboard()->setText(username);
        });
    }
    menu.addAction("Copy User ID", this, [userId] {
        QApplication::clipboard()->setText(QString::number(userId));
    });

    // Moderation (guild context, not self)
    if (!m_inDmMode && m_currentGuildId > 0 && userId != m_me.id) {
        bool canKick = false, canBan = false;
        for (const auto& me : m_members) {
            if (me.userId == m_me.id) {
                if (me.role == "owner") { canKick = canBan = true; break; }
                for (const auto& r : me.roles) {
                    if (r.permissions & (1 << 7)) { canKick = canBan = true; break; } // ADMINISTRATOR
                    if (r.permissions & (1 << 3)) canKick = true;  // KICK
                    if (r.permissions & (1 << 4)) canBan  = true;  // BAN
                }
                break;
            }
        }
        if (canKick || canBan) {
            menu.addSeparator();
            if (canKick) {
                menu.addAction("Kick Member", this, [this, userId, shownName] {
                    if (!ForjPrompt::question(this, "Kick Member",
                            QStringLiteral("Kick %1?").arg(shownName)))
                        return;
                    m_api->kickMember(m_currentGuildId, userId, [this](bool ok, const QString& err) {
                        if (ok) loadMembers(m_currentGuildId);
                        else statusBar()->showMessage("Error: " + err, 4000);
                    });
                });
            }
            if (canBan) {
                menu.addAction("Ban Member", this, [this, userId, shownName] {
                    const QString reason = ForjPrompt::getText(this, "Ban Member",
                        QStringLiteral("Reason for banning %1:").arg(shownName));
                    m_api->banMember(m_currentGuildId, userId, reason,
                        [this](bool ok, const QString& err) {
                            if (ok) loadMembers(m_currentGuildId);
                            else statusBar()->showMessage("Error: " + err, 4000);
                        });
                });
            }
        }
    }

    menu.exec(globalPos);
}

// ── Guild icon helpers ────────────────────────────────────────────────────────

void MainWindow::fetchGuildIconThen(int guildId, bool hasIcon,
                                    std::function<void(const QIcon&)> cb)
{
    if (m_guildIconCache.contains(guildId)) {
        cb(m_guildIconCache[guildId]);
        return;
    }

    // Build a fallback initial-letter icon inline
    auto makeFallback = [this, guildId]() -> QIcon {
        const GuildInfo* gp = nullptr;
        for (const auto& g : m_guilds) {
            if (g.id == guildId) { gp = &g; break; }
        }
        const QString initial = gp ? gp->name.left(1).toUpper() : "?";
        QPixmap pix(36, 36);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(colorForId(guildId)));
        p.setPen(Qt::NoPen);
        p.drawEllipse(0, 0, 36, 36);
        p.setPen(Qt::white);
        p.setFont(QFont("Arial", 14, QFont::Bold));
        p.drawText(QRect(0, 0, 36, 36), Qt::AlignCenter, initial);
        return QIcon(pix);
    };

    if (!hasIcon) {
        QIcon icon = makeFallback();
        m_guildIconCache[guildId] = icon;
        cb(icon);
        return;
    }

    if (m_guildIconFetching.contains(guildId)) return;
    m_guildIconFetching.insert(guildId);
    m_api->getGuildIcon(guildId, [this, guildId, makeFallback, cb](bool ok, const QByteArray& data) {
        m_guildIconFetching.remove(guildId);
        QPixmap pix;
        if (!ok || data.isEmpty() || !pix.loadFromData(data))
            pix = makeFallback().pixmap(36, 36);

        // Produce rounded pixmap
        QPixmap rounded(36, 36);
        rounded.fill(Qt::transparent);
        QPainter p(&rounded);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addEllipse(0, 0, 36, 36);
        p.setClipPath(path);
        p.drawPixmap(0, 0, pix.scaled(36, 36, Qt::KeepAspectRatioByExpanding,
                                       Qt::SmoothTransformation));
        QIcon icon(rounded);
        m_guildIconCache[guildId] = icon;
        cb(icon);
    });
}

// ── WebSocket events ──────────────────────────────────────────────────────────

void MainWindow::onNewMessage(const MessageInfo& msg)
{
    // Skip messages we already rendered from the REST send callback
    if (msg.authorId == m_me.id)
        return;
    if (!m_inDmMode && msg.channelId == m_currentChannelId) {
        appendGuildMessage(msg);
        m_messageView->verticalScrollBar()->setValue(
            m_messageView->verticalScrollBar()->maximum());
    } else {
        // Increment unread for this channel if it's visible in the current channel list
        for (int i = 0; i < m_channels.size(); ++i) {
            if (m_channels[i].id == msg.channelId) {
                m_unreadChannels[msg.channelId]++;
                const int cnt = m_unreadChannels[msg.channelId];
                const bool isMention = m_mentionChannels.contains(msg.channelId);
                if (auto* item = m_channelList->item(i)) {
                    item->setText(QStringLiteral("# %1 (%2)").arg(m_channels[i].name).arg(cnt));
                    QFont f = item->font(); f.setBold(true); item->setFont(f);
                    item->setForeground(isMention ? QColor(0xED, 0x4C, 0x5E)
                                                  : QColor(0xDC, 0xDD, 0xDE));
                }
                break;
            }
        }
        // Update guild-level unread indicator
        const int guildId = m_channelGuildMap.value(msg.channelId, 0);
        if (guildId > 0 && guildId != m_currentGuildId) {
            m_unreadGuilds.insert(guildId);
            for (int i = 0; i < m_guilds.size(); ++i) {
                if (m_guilds[i].id == guildId) {
                    if (auto* item = m_guildList->item(i))
                        item->setForeground(QColor(0xFF, 0xA0, 0x00));
                    break;
                }
            }
        }
    }
}

void MainWindow::onNewDmMessage(const DmMessageInfo& msg)
{
    // Skip our own DM messages — already rendered from the sendDm REST callback
    if (msg.authorId == m_me.id)
        return;
    if (m_inDmMode && msg.conversationId == m_currentDmId) {
        appendDmMessage(msg);
        m_messageView->verticalScrollBar()->setValue(
            m_messageView->verticalScrollBar()->maximum());
    } else {
        // Unread badge
        m_unreadDms[msg.conversationId]++;
        bool found = false;
        for (int i = 0; i < m_dms.size(); ++i) {
            if (m_dms[i].id == msg.conversationId) {
                const QString name = m_dms[i].otherDisplayName.isEmpty()
                                     ? m_dms[i].otherUsername
                                     : m_dms[i].otherDisplayName;
                const int cnt = m_unreadDms[msg.conversationId];
                if (auto* item = m_dmList->item(i)) {
                    item->setText(QStringLiteral("@ %1 (%2)").arg(name).arg(cnt));
                    QFont f = item->font(); f.setBold(true); item->setFont(f);
                }
                found = true;
                break;
            }
        }
        if (!found) loadDms(); // new conversation we don't know yet

        // Add to notification inbox
        NotificationItem notif;
        notif.type      = NotificationItem::Type::DM;
        notif.title     = QStringLiteral("DM from @%1").arg(msg.authorUsername);
        notif.body      = msg.content.left(100);
        notif.timestamp = QDateTime::currentDateTime().toString("hh:mm");
        notif.dmId      = msg.conversationId;
        m_notifications.append(notif);
        m_unreadNotifications++;
        if (m_notifyAction)
            m_notifyAction->setText(QStringLiteral("\U0001F514 (%1)").arg(m_unreadNotifications));

        // Tray notification
        m_trayIcon->showMessage(
            notif.title, notif.body, QSystemTrayIcon::Information, 5000);
        QApplication::alert(this, 3000);
    }
}

void MainWindow::onUserOnline(int userId, const QString& username)
{
    onUserStatusChanged(userId, username, "online");
}

void MainWindow::onUserOffline(int userId, const QString& username)
{
    onUserStatusChanged(userId, username, "offline");
}

void MainWindow::onUserStatusChanged(int userId, const QString& /*username*/, const QString& status)
{
    const bool online = (status != "offline");
    for (int i = 0; i < m_members.size(); ++i) {
        if (m_members[i].userId == userId) {
            m_members[i].isOnline = online;
            m_members[i].status   = status;
            if (auto* item = m_memberList->item(i)) {
                item->setForeground(online ? QColor(0xDC,0xDD,0xDE) : QColor(0x4F,0x54,0x5C));
                item->setData(kStatusDotRole, status);
            }
            break;
        }
    }
}

void MainWindow::onMentioned(int guildId, int channelId, const QString& channelName,
                              const QString& author, const QString& content)
{
    const bool viewingIt = !m_inDmMode && m_currentChannelId == channelId;

    if (!viewingIt) {
        // Mark channel as having a @mention
        m_mentionChannels.insert(channelId);
        m_unreadChannels[channelId]++;
        const int cnt = m_unreadChannels[channelId];

        // Update channel list item (if currently visible)
        for (int i = 0; i < m_channels.size(); ++i) {
            if (m_channels[i].id == channelId) {
                if (auto* item = m_channelList->item(i)) {
                    item->setText(QStringLiteral("# %1 (%2)").arg(m_channels[i].name).arg(cnt));
                    QFont f = item->font(); f.setBold(true); item->setFont(f);
                    item->setForeground(QColor(0xED, 0x4C, 0x5E)); // red for mention
                }
                break;
            }
        }
        // Update guild badge
        if (guildId != m_currentGuildId) {
            m_unreadGuilds.insert(guildId);
            for (int i = 0; i < m_guilds.size(); ++i) {
                if (m_guilds[i].id == guildId) {
                    if (auto* item = m_guildList->item(i))
                        item->setForeground(QColor(0xED, 0x4C, 0x5E));
                    break;
                }
            }
        }
        QApplication::alert(this, 3000);
    }

    // Add to notification inbox
    NotificationItem notif;
    notif.type      = NotificationItem::Type::Mention;
    notif.title     = QStringLiteral("Mentioned in #%1").arg(channelName);
    notif.body      = QStringLiteral("@%1: %2").arg(author, content.left(100));
    notif.timestamp = QDateTime::currentDateTime().toString("hh:mm");
    notif.guildId   = guildId;
    notif.channelId = channelId;
    m_notifications.append(notif);
    m_unreadNotifications++;
    if (m_notifyAction)
        m_notifyAction->setText(QStringLiteral("\U0001F514 (%1)").arg(m_unreadNotifications));

    // Tray notification
    m_trayIcon->showMessage(
        notif.title, notif.body, QSystemTrayIcon::Information, 6000);
}

void MainWindow::onInputChanged(const QString& text)
{
    if (!text.startsWith('/') && m_completer->popup()->isVisible())
        m_completer->popup()->hide();
}

// ── Voice handlers ────────────────────────────────────────────────────────────

void MainWindow::onLeaveVoiceClicked()
{
    if (m_currentVoiceChannelId == 0) return;
    const int chId = m_currentVoiceChannelId;

    // Stop audio engine first
    if (m_voiceEngine) {
        m_voiceEngine->stop();
        m_voiceEngine->deleteLater();
        m_voiceEngine = nullptr;
    }
    m_voiceSpeakingState.clear();
    m_voiceTiles.clear();

    m_api->leaveVoice(chId, [this, chId](bool /*ok*/, const QString&) {
        m_currentVoiceChannelId = 0;
        m_voiceBar->setVisible(false);
        m_voiceMuted    = false;
        m_voiceDeafened = false;
        m_voiceMuteBtn->setChecked(false);
        m_voiceDeafenBtn->setChecked(false);
        m_chatStack->setCurrentIndex(0);
        updateVoiceChannelItem(chId);
    });
}

void MainWindow::onVoiceJoin(int channelId, int userId, const QString& username)
{
    auto& ps = m_voiceParticipants[channelId];
    // Avoid duplicates
    for (const auto& p : ps) { if (p.userId == userId) return; }
    ps.append({userId, username});
    updateVoiceChannelItem(channelId);
    if (m_chatStack->currentIndex() == 1 && channelId == m_currentVoiceChannelId)
        refreshVoiceRoomTiles();
}

void MainWindow::onVoiceLeave(int channelId, int userId, const QString& /*username*/)
{
    auto& ps = m_voiceParticipants[channelId];
    for (int i = 0; i < ps.size(); ++i) {
        if (ps[i].userId == userId) { ps.removeAt(i); break; }
    }
    m_voiceSpeakingState.remove(userId);
    m_voiceTiles.remove(userId);
    updateVoiceChannelItem(channelId);
    if (m_chatStack->currentIndex() == 1 && channelId == m_currentVoiceChannelId)
        refreshVoiceRoomTiles();
}

void MainWindow::onVoiceSpeaking(int channelId, int userId, bool speaking)
{
    m_voiceSpeakingState[userId] = speaking;
    if (channelId == m_currentVoiceChannelId) {
        if (auto* w = m_voiceTiles.value(userId))
            static_cast<VoiceAvatarWidget*>(w)->setSpeaking(speaking);
    }
}

// ── Rendering helpers ─────────────────────────────────────────────────────────

void MainWindow::appendGuildMessage(const MessageInfo& msg)
{
    QDateTime dt = QDateTime::fromString(msg.createdAt, Qt::ISODate);
    if (!dt.isValid()) dt = QDateTime::fromString(msg.createdAt, Qt::ISODateWithMs);
    const QString ts = dt.isValid() ? dt.toLocalTime().toString("hh:mm") : msg.createdAt.left(16);

    // Resolve server nickname and role
    QString displayName = msg.authorUsername;
    QString roleColor, roleName;
    for (const auto& m : m_members) {
        if (m.userId == msg.authorId) {
            if (!m.nickname.isEmpty())         displayName = m.nickname;
            else if (!m.displayName.isEmpty()) displayName = m.displayName;
            if (m.role == "owner") {
                roleName  = "Server Owner";
                roleColor = "#FAA61A";
            } else {
                const RoleInfo* top = nullptr;
                for (const auto& r : m.roles)
                    if (!top || r.position > top->position) top = &r;
                if (top) { roleName = top->name; roleColor = top->color; }
            }
            break;
        }
    }

    const QString rc = roleColor, rn = roleName, dn = displayName;
    fetchMemberAvatarThen(m_currentGuildId, msg.authorId,
                          [this, msg, ts, dn, rc, rn](const QString& avatarCell) {
        appendChatLine(dn, msg.authorId, msg.content, ts, msg.authorIsBot, avatarCell, rc, rn,
                       msg.id, msg.replyToAuthor, msg.replyToContent);
    });
}

void MainWindow::appendDmMessage(const DmMessageInfo& msg)
{
    QDateTime dt = QDateTime::fromString(msg.createdAt, Qt::ISODate);
    if (!dt.isValid()) dt = QDateTime::fromString(msg.createdAt, Qt::ISODateWithMs);
    const QString ts = dt.isValid() ? dt.toLocalTime().toString("hh:mm") : msg.createdAt.left(16);
    // For own messages, also check m_me.hasAvatar in case it's fresher than the msg flag
    bool hasAv = msg.authorHasAvatar;
    if (msg.authorId == m_me.id) hasAv = hasAv || m_me.hasAvatar;
    fetchAvatarThen(msg.authorId, hasAv, [this, msg, ts](const QString& avatarCell) {
        appendChatLine(msg.authorUsername, msg.authorId, msg.content, ts, msg.authorIsBot,
                       avatarCell, {}, {}, msg.id, msg.replyToAuthor, msg.replyToContent);
    });
}

void MainWindow::appendChatLine(const QString& author, int authorId,
                                 const QString& content, const QString& timestamp,
                                 bool isBot, const QString& avatarCell,
                                 const QString& roleColor, const QString& roleName,
                                 int msgId,
                                 const QString& replyToAuthor,
                                 const QString& replyToContent)
{
    // Cache msg info for later reply-button clicks
    if (msgId > 0)
        m_msgReplyCache[msgId] = qMakePair(author, content.left(120));

    // ── Reply quote ───────────────────────────────────────────────────────────
    QString replyHtml;
    if (!replyToAuthor.isEmpty()) {
        const QString preview = replyToContent.left(80)
            + (replyToContent.size() > 80 ? "…" : "");
        replyHtml = QStringLiteral(
            "<span style='color:#5865F2;font-size:small;'>&#x21A9; @%1&nbsp;</span>"
            "<span style='color:#72767D;font-size:small;font-style:italic;'>%2</span><br>")
            .arg(replyToAuthor.toHtmlEscaped(),
                 preview.toHtmlEscaped());
    }

    // ── @mention highlighting in content ─────────────────────────────────────
    QString contentHtml = content.toHtmlEscaped().replace('\n', "<br>");
    {
        static const QRegularExpression kMentionRe(QStringLiteral("@([a-zA-Z0-9_]+)"));
        QRegularExpressionMatchIterator it = kMentionRe.globalMatch(contentHtml);
        int offset = 0;
        QString hlContent;
        while (it.hasNext()) {
            const auto match = it.next();
            hlContent += contentHtml.mid(offset, match.capturedStart() - offset);
            const QString uname = match.captured(1);
            if (uname.compare(m_me.username, Qt::CaseInsensitive) == 0) {
                hlContent += QStringLiteral(
                    "<span style='color:#ffffff;background-color:#5865F2;"
                    "border-radius:2px;padding:0 2px;'>@%1</span>").arg(uname);
            } else {
                hlContent += QStringLiteral("<span style='color:#5865F2;'>@%1</span>").arg(uname);
            }
            offset = match.capturedEnd();
        }
        hlContent += contentHtml.mid(offset);
        contentHtml = hlContent;
    }

    // ── Reply button ──────────────────────────────────────────────────────────
    QString replyBtn;
    if (msgId > 0) {
        replyBtn = QStringLiteral(
            "&nbsp;<a href='forj://reply/%1' "
            "style='color:#72767D;text-decoration:none;font-size:x-small;' "
            "title='Reply'>&#x21A9;</a>").arg(msgId);
    }

    const QString nameColor = roleColor.isEmpty() ? colorForId(authorId) : roleColor;
    const QString roleTag   = roleName.isEmpty() ? QString{}
        : QStringLiteral("&nbsp;<span style='color:#72767D;font-size:x-small;'>[%1]</span>")
            .arg(roleName.toHtmlEscaped());
    const QString nameHtml = QStringLiteral(
        "<a href='forj://user/%1' style='color:%2;font-weight:bold;text-decoration:none;'>%3</a>")
        .arg(authorId)
        .arg(nameColor)
        .arg((author + (isBot ? " \U0001F916" : "")).toHtmlEscaped());
    const QString html = QStringLiteral(
        "<table cellpadding='2' cellspacing='0' style='margin-bottom:4px;'>"
        "<tr>"
        "<td width='42' valign='top'>%1</td>"
        "<td valign='top'>"
          "%2"
          "%3"
          "&nbsp;<span style='color:#72767D;font-size:small;'>%4</span>"
          "%5"
          "<br>%6<span>%7</span>"
        "</td>"
        "</tr></table>")
        .arg(avatarCell, nameHtml, roleTag,
             timestamp.toHtmlEscaped(),
             replyBtn,
             replyHtml,
             contentHtml);

    m_messageView->append(html);
}

void MainWindow::fetchMemberIconThen(int guildId, int userId,
                                     std::function<void(const QIcon&)> cb)
{
    const QString key = QStringLiteral("%1:%2").arg(guildId).arg(userId);
    if (m_memberIconCache.contains(key)) { cb(m_memberIconCache[key]); return; }

    const auto buildFallback = [this, userId]() -> QIcon {
        QPixmap pix(34, 34);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(colorForId(userId)));
        p.setPen(Qt::NoPen);
        p.drawEllipse(0, 0, 34, 34);
        return QIcon(pix);
    };

    m_memberIconPending[key].append(std::move(cb));
    if (m_memberIconFetching.contains(key)) return;

    m_memberIconFetching.insert(key);
    m_api->getMemberAvatar(guildId, userId,
        [this, key, buildFallback](bool ok, const QByteArray& data) {
            m_memberIconFetching.remove(key);
            QIcon result;
            if (ok && !data.isEmpty()) {
                QPixmap src;
                if (src.loadFromData(data)) {
                    QPixmap scaled = src.scaled(34, 34, Qt::KeepAspectRatioByExpanding,
                                                Qt::SmoothTransformation);
                    const int ox = (scaled.width()  - 34) / 2;
                    const int oy = (scaled.height() - 34) / 2;
                    QPixmap pix(34, 34);
                    pix.fill(Qt::transparent);
                    QPainter p(&pix);
                    p.setRenderHint(QPainter::Antialiasing);
                    QPainterPath path;
                    path.addEllipse(0, 0, 34, 34);
                    p.setClipPath(path);
                    p.drawPixmap(0, 0, scaled, ox, oy, 34, 34);
                    result = QIcon(pix);
                }
            }
            if (result.isNull()) result = buildFallback();
            m_memberIconCache[key] = result;
            const auto cbs = m_memberIconPending.take(key);
            for (const auto& c : cbs) c(result);
        });
}

void MainWindow::fetchMemberAvatarThen(int guildId, int userId,
                                       std::function<void(const QString&)> cb)
{
    const QString key = QStringLiteral("%1:%2").arg(guildId).arg(userId);

    if (m_memberAvatarCache.contains(key)) { cb(m_memberAvatarCache[key]); return; }

    const auto buildFallback = [this, userId]() -> QString {
        return colorCircleHtml(colorForId(userId), 34);
    };

    m_memberAvatarPending[key].append(std::move(cb));
    if (m_memberAvatarFetching.contains(key)) return;

    m_memberAvatarFetching.insert(key);
    m_api->getMemberAvatar(guildId, userId,
        [this, key, buildFallback](bool ok, const QByteArray& data) {
            m_memberAvatarFetching.remove(key);
            QString result;
            if (ok && !data.isEmpty()) {
                result = circularAvatarHtml(data, 34);
                if (result.isEmpty()) result = buildFallback();
            } else {
                result = buildFallback();
            }
            m_memberAvatarCache[key] = result;
            const auto cbs = m_memberAvatarPending.take(key);
            for (const auto& c : cbs) c(result);
        });
}

void MainWindow::fetchAvatarThen(int userId, bool hasAvatar,
                                  std::function<void(const QString&)> cb)
{
    const auto buildFallback = [this, userId]() -> QString {
        return colorCircleHtml(colorForId(userId), 34);
    };

    // Don't serve (or cache) a stale fallback when the user might have gained an
    // avatar since we last checked.  Only cached *real* avatars are trusted.
    if (!hasAvatar) {
        cb(buildFallback());
        return;
    }

    if (m_avatarCache.contains(userId)) { cb(m_avatarCache[userId]); return; }

    // Queue callback; if already fetching we'll fire it when done
    m_avatarPending[userId].append(std::move(cb));
    if (m_avatarFetching.contains(userId)) return;

    m_avatarFetching.insert(userId);
    m_api->getAvatar(userId, [this, userId, buildFallback](bool ok, const QByteArray& data) {
        m_avatarFetching.remove(userId);
        QString result;
        if (ok && !data.isEmpty()) {
            result = circularAvatarHtml(data, 34);
            if (result.isEmpty()) result = buildFallback();
        } else {
            result = buildFallback();
        }
        m_avatarCache[userId] = result;
        const auto cbs = m_avatarPending.take(userId);
        for (const auto& c : cbs) c(result);
    });
}

void MainWindow::refreshMemberOnlineStatus(int userId, bool online)
{
    onUserStatusChanged(userId, {}, online ? "online" : "offline");
}

void MainWindow::updateMyStatusDot()
{
    if (!m_myStatusDotLabel) return;
    m_myStatusDotLabel->setPixmap(makeStatusDot(m_myStatus, 14));
}

// ── Private helpers ───────────────────────────────────────────────────────────

void MainWindow::openDmWithUser(const UserInfo& user)
{
    m_api->openDm(user.id,
        [this](bool ok, const QString& err, const DmConversationInfo& dm) {
            if (!ok) { ForjPrompt::warning(this, "Error", err); return; }
            // Find or insert in list
            int row = -1;
            for (int i = 0; i < m_dms.size(); ++i) {
                if (m_dms[i].id == dm.id) { row = i; break; }
            }
            if (row == -1) {
                row = m_dms.size();
                m_dms.append(dm);
                const QString name = dm.otherDisplayName.isEmpty()
                                     ? dm.otherUsername : dm.otherDisplayName;
                m_dmList->addItem("@ " + name);
            }
            m_dmList->setCurrentRow(row);
        });
}

// ── Window title change → update custom titlebar ──────────────────────────────

void MainWindow::changeEvent(QEvent* e)
{
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::WindowTitleChange && m_titleBar)
        m_titleBar->setTitle(windowTitle());
}

void MainWindow::paintEvent(QPaintEvent*)
{
    // Paint rounded-rect background so corners are smooth with WA_TranslucentBackground
    static constexpr int kRadius = 8;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x36, 0x39, 0x3F));
    p.drawRoundedRect(rect(), kRadius, kRadius);
}

void MainWindow::resizeEvent(QResizeEvent* e)
{
    // Keep mask in sync so input/clipping follows the rounded shape
    static constexpr int kRadius = 8;
    QBitmap bm(size());
    bm.fill(Qt::color0);
    QPainter p(&bm);
    p.setBrush(Qt::color1);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(rect(), kRadius, kRadius);
    setMask(bm);
    QMainWindow::resizeEvent(e);
}

// ── Windows: resize regions via WM_NCHITTEST ──────────────────────────────────

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (eventType == "windows_generic_MSG") {
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_NCHITTEST && !isMaximized()) {
            constexpr int kBorder = 5;
            const QPoint pos = mapFromGlobal(QCursor::pos());
            const int w = width(), h = height();
            const bool left  = pos.x() < kBorder;
            const bool right = pos.x() > w - kBorder;
            const bool top   = pos.y() < kBorder;
            const bool bot   = pos.y() > h - kBorder;
            if (left  && top)  { *result = HTTOPLEFT;     return true; }
            if (right && top)  { *result = HTTOPRIGHT;    return true; }
            if (left  && bot)  { *result = HTBOTTOMLEFT;  return true; }
            if (right && bot)  { *result = HTBOTTOMRIGHT; return true; }
            if (left)          { *result = HTLEFT;        return true; }
            if (right)         { *result = HTRIGHT;       return true; }
            if (top)           { *result = HTTOP;         return true; }
            if (bot)           { *result = HTBOTTOM;      return true; }
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif
