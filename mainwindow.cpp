#include "mainwindow.hpp"

#include <Section.h>
#include <fmt/chrono.h>
#include <fmt/core.h>
#include <fmt/ranges.h>

#include <QApplication>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaEnum>
#include <QPair>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>
#include <QTime>
#include <QUrl>
#include <QVBoxLayout>
#include <QVector>
#include <chrono>
#include <ciso646>
#include <cuchar>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <timedialog.hpp>

#include "./ui_mainwindow.h"
#include "listdialog.hpp"
#include "processwidget.hpp"
#include "videoinfodialog.hpp"
#include "videoinfowidget.hpp"

namespace {
concat::VideoInfo retrieve_input_info(const concat::VideoInfo &info) {
    auto result = concat::VideoInfo::create_input_info();
    if (std::holds_alternative<QSize>(info.resolution)) {
        auto resolution = std::get<QSize>(info.resolution);
        result.resolution = concat::ValueRange<QSize>{resolution, resolution};
    }
    if (std::holds_alternative<double>(info.framerate)) {
        auto framerate = std::get<double>(info.framerate);
        result.framerate = concat::ValueRange<double>{framerate, framerate};
    }
    if (std::holds_alternative<QString>(info.audio_codec)) {
        result.audio_codec = QSet<QString>{std::get<QString>(info.audio_codec)};
    }
    if (std::holds_alternative<QString>(info.video_codec)) {
        result.video_codec = QSet<QString>{std::get<QString>(info.video_codec)};
    }
    // encoding_argsはinitial_valueで指定すべき
    return result;
}
constexpr auto INITIAL_ANIMATION_DURATION = 200;
}  // namespace
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui_(new Ui::MainWindow) {
    ui_->setupUi(this);
#ifndef NDEBUG
    this->setWindowTitle(QStringLiteral("%1 (debug build)").arg(this->windowTitle()));
#endif

    connect(ui_->actionopen, &QAction::triggered, this, &MainWindow::open_video_);
    connect(ui_->pushButton_save, &QPushButton::pressed, this, &MainWindow::save_result_);
    connect(ui_->actiondefault_extractor, &QAction::triggered, this, &MainWindow::select_default_chaptername_plugin_);
    connect(ui_->actionsavefile_name_generator, &QAction::triggered, this, &MainWindow::select_savefile_name_plugin_);
    connect(ui_->actioneffective_period_of_cache, &QAction::triggered, this,
            &MainWindow::update_effective_period_of_cache_);
    connect(ui_->actiondefault_video_info, &QAction::triggered, this, &MainWindow::edit_default_video_info_);
    connect(ui_->actionanimation_duration_of_collapsible_section, &QAction::triggered, this,
            &MainWindow::update_animation_duration);
    QDir settings_dir(QApplication::applicationDirPath() + "/settings");
    if (QDir().mkpath(settings_dir.absolutePath())) {  // QDir::mkpath() returns true even when path already exists
        settings_ = new QSettings(settings_dir.filePath("settings.ini"), QSettings::IniFormat);
    }
    auto section = new ui::Section(tr("video info"),
                                   settings_->value("animation_duration", INITIAL_ANIMATION_DURATION).toInt(), this);
    // メインウィンドウなので基本的に起動から終了まで存在するので、明示的にdeleteしなくても問題はない。
    auto layout = new QGridLayout();
    video_info_widget_ = new VideoInfoWidget(section);
    layout->addWidget(video_info_widget_);
    section->setContentLayout(*layout);
    ui_->gridLayout_section->addWidget(section);
    if (settings_->contains("default_video_info")) {
        auto default_video_info = settings_->value("default_video_info").value<concat::VideoInfo>();
        video_info_widget_->set_infos(default_video_info, retrieve_input_info(default_video_info));
    }
}

MainWindow::~MainWindow() {
    delete ui_;
    if (settings_ != nullptr) {
        settings_->deleteLater();
    }
    if (tmpdir_ != nullptr) {
        delete tmpdir_;
    }
}
QUrl MainWindow::read_video_dir_cache_() {
    settings_->beginGroup("video_dir_cache");
    auto last_saved = settings_->value("last_saved").toDateTime().toMSecsSinceEpoch();
    auto effective_period = settings_->value("effective_period").toTime().msecsSinceStartOfDay();
    auto dir = settings_->value("dir").toUrl();
    settings_->endGroup();
    if (QDateTime::currentDateTime().toMSecsSinceEpoch() - last_saved < effective_period) {
        return dir;
    } else {
        return QUrl();
    }
}
void MainWindow::write_video_dir_cache_(QUrl dir) {
    settings_->beginGroup("video_dir_cache");
    settings_->setValue("last_saved", QDateTime::currentDateTime());
    // effective period is written in update_effective_period_of_cache()
    settings_->setValue("dir", dir);
    settings_->endGroup();
}
void MainWindow::update_effective_period_of_cache_() {
    bool confirmed = false;
    auto period = TimeDialog::get_time(nullptr, tr("effective period of cache"), tr("enter effective period of cache"),
                                       settings_->value("video_dir_cache/effective_period").toTime(), &confirmed);
    if (confirmed && period.isValid()) {
        settings_->setValue("video_dir_cache/effective_period", period);
    }
}

void MainWindow::update_animation_duration() {
    bool confirmed = false;
    auto period = QInputDialog::getInt(
        nullptr, tr("animation duration"),
        tr("enter animation duration of collapsible section in milliseconds (restart is required)"),
        settings_->value("animation_duration", INITIAL_ANIMATION_DURATION).toInt(), 0, INT_MAX, 1, &confirmed);
    if (confirmed) {
        settings_->setValue("animation_duration", period);
    }
}

void MainWindow::edit_default_video_info_() {
    bool confirmed = false;
    auto default_video_info = settings_->value("default_video_info").value<concat::VideoInfo>();
    auto default_input_video_info = retrieve_input_info(default_video_info);
    auto video_info = VideoInfoDialog::get_video_info(
        nullptr, tr("effective period of cache"), tr("enter effective period of cache"),
        settings_->value("default_video_info").value<concat::VideoInfo>(), default_input_video_info, &confirmed);
    if (confirmed) {
        settings_->setValue("default_video_info", QVariant::fromValue(video_info));
    }
}

void MainWindow::open_video_() {
    auto videodirs = QStandardPaths::standardLocations(QStandardPaths::MoviesLocation);
    auto videodir = read_video_dir_cache_();
    if (videodir.isEmpty() && not videodirs.isEmpty()) {
        videodir = QUrl::fromLocalFile(videodirs[0]);
    }
    auto filenames =
        QFileDialog::getOpenFileNames(this, tr("open video file"), videodir.toLocalFile(), tr("Videos (*.mp4)"));
    if (filenames.isEmpty()) {
        QMessageBox::warning(nullptr, tr("warning"), tr("no file was selected"));
        return;
    }
    QDir filepath{filenames[0]};
    filepath.cdUp();
    write_video_dir_cache_(QUrl::fromLocalFile(filepath.path()));
    ui_->listWidget_filenames->addItems(filenames);
    ui_->pushButton_save->setEnabled(true);
}
namespace impl_ {
constexpr Qt::ConnectionType ONESHOT_AUTO_CONNECTION =
    static_cast<Qt::ConnectionType>(Qt::AutoConnection | Qt::SingleShotConnection);
class OnTrue {
    std::function<void(void)> func_;

   public:
    OnTrue(std::function<void(void)> func) : func_(func) {}
    void operator()(bool arg) {
        if (arg) {
            this->func_();
        }
    }
};
constexpr auto INVALID_SIZE = static_cast<std::uintmax_t>(-1);
#ifndef __STDC_UTF_16__
static_assert(false, "encoding of char16_t is not guaranteed to be UTF-16");
#endif
QString format_size(std::uintmax_t size) {
    // NOLINTBEGIN(readability-identifier-naming)
    // these will divide size, so shouldn't be integral type. Otherwise, result will be rounded.
    constexpr double KiB = 1024;
    constexpr double MiB = 1024 * KiB;
    constexpr double GiB = 1024 * MiB;
    constexpr double TiB = 1024 * GiB;
    // NOLINTEND(readability-identifier-naming)
    std::stringstream result;
    result << std::fixed << std::setprecision(2);
    if (size == INVALID_SIZE) {
        result << "N/A";
    } else if (size < 1 * KiB) {
        result << size << "B";
    } else if (size < 1 * MiB) {
        result << (size / KiB) << "KiB";
    } else if (size < 1 * GiB) {
        result << (size / MiB) << "MiB";
    } else if (size < 1 * TiB) {
        result << (size / GiB) << "GiB";
    } else {
        result << (size / TiB) << "TiB";
    }
    return QString::fromStdString(result.str());
}
QString format_path(std::filesystem::path path) {
    if (path.empty()) {
        return "N/A";
    } else {
        return QString::fromStdU16String(path.u16string());
    }
}
}  // namespace impl_
void MainWindow::show_size_() {
    namespace fs = std::filesystem;
    std::uintmax_t tmpdir_available_size = impl_::INVALID_SIZE;
    fs::path tmpdir{};
    std::uintmax_t dstdir_available_size = impl_::INVALID_SIZE;
    fs::path dstdir{};
    std::uintmax_t estimated_result_size = impl_::INVALID_SIZE;
    bool error_has_ocurred = false;
    auto on_error = [&error_has_ocurred](std::exception &e) {
        QMessageBox::critical(nullptr, tr("error"), QString::fromLocal8Bit(e.what()));
        error_has_ocurred = true;
    };
    try {
        tmpdir = fs::canonical(tmpdir_->path().toStdU16String());
        auto tmpdir_size = fs::space(tmpdir).available;
        if (tmpdir_size != static_cast<std::uintmax_t>(-1)) {
            tmpdir_available_size = tmpdir_size;
        }
    } catch (std::exception &e) {
        on_error(e);
    }
    for (auto i = 0; i < ui_->listWidget_filenames->count(); i++) {
        fs::path filepath{};
        try {
            filepath = ui_->listWidget_filenames->item(i)->text().toStdU16String();
            filepath = fs::canonical(filepath);
        } catch (std::exception &e) {
            on_error(e);
            continue;  // 以下の処理はfilepathに依存する
        }
        if (i == 0) {
            try {
                dstdir = filepath.parent_path();
                auto dstdir_size = fs::space(dstdir).available;
                if (dstdir_size != static_cast<std::uintmax_t>(-1)) {
                    dstdir_available_size = dstdir_size;
                }
            } catch (std::exception &e) {
                on_error(e);
            }
        }
        try {
            auto size = fs::file_size(filepath);
            if (estimated_result_size == impl_::INVALID_SIZE) {
                estimated_result_size = size;
            } else {
                estimated_result_size += size;
            }
        } catch (std::exception &e) {
            on_error(e);
        }
    }
    QString message;
    message += "<h1>" + tr("size informations") + "</h1>";
    message += "<b>";
    message += tr("when videos are re-encoded(e.g. when video-codec is changed), estimation will be inaccurate.");
    message += "</b>";
    message += "<p>";
    message += (error_has_ocurred ? tr("warning: some error has ocurred. result may be incorrect")
                                  : tr("no error has ocurred"));
    message += "</p>";
    message += "<h2>" + tr("necessary space") + "</h2>";
    message += "<p>";
    message += tr("estimated result size: %1").arg(impl_::format_size(estimated_result_size)) + "<br>";
    message += tr("(2*estimated result size: %1)").arg(impl_::format_size(2 * estimated_result_size));
    message += "</p>";
    message += "<h2>" + tr("available space") + "</h2>";
    message += "<p>";
    message += tr("%1: %2").arg(impl_::format_path(tmpdir)).arg(impl_::format_size(tmpdir_available_size)) + "<br>";
    message += tr("%1: %2").arg(impl_::format_path(dstdir)).arg(impl_::format_size(dstdir_available_size));
    message += "</p>";
    message += "<p>" + tr("do you want to proceed?") + "</p>";
    switch (QMessageBox::question(nullptr, "size info", message)) {
        case QMessageBox::Yes:
            create_savefile_name_();
            break;
        case QMessageBox::No:
            break;
        default:
            Q_UNREACHABLE();
    }
}
void MainWindow::create_savefile_name_() {
    QString filename = QUrl::fromLocalFile(ui_->listWidget_filenames->item(current_index_)->text()).fileName();
    if (settings_->contains("savefile_name_plugin") && settings_->value("savefile_name_plugin") != NO_PLUGIN) {
        process_->start(
            PYTHON,
            {savefile_name_plugins_dir_().absoluteFilePath(settings_->value("savefile_name_plugin").toString()),
             filename},
            false);
        connect(process_, &ProcessWidget::finished, this, impl_::OnTrue([=] { this->confirm_savefile_name_(); }),
                impl_::ONESHOT_AUTO_CONNECTION);
    } else {
        confirm_savefile_name_();
    }
}
void MainWindow::confirm_savefile_name_() {
    auto source_filepath = QUrl::fromLocalFile(ui_->listWidget_filenames->item(0)->text());
    QString default_savefile_name = source_filepath.fileName();
    if (settings_->contains("savefile_name_plugin") && settings_->value("savefile_name_plugin") != NO_PLUGIN) {
        default_savefile_name = process_->get_stdout();
    }
    bool confirmed = false;
    QString save_filename;
    QUrl save_filepath;
    do {
        save_filename = QInputDialog::getText(this, tr("savefile name"), tr("enter file name of result"),
                                              QLineEdit::Normal, default_savefile_name, &confirmed);
        save_filepath = QUrl(source_filepath.toString(QUrl::RemoveFilename) + save_filename);
        if (not confirmed) {
            return;
        }
        if (save_filename.isEmpty()) {
            auto button = QMessageBox::warning(this, tr("empty filename"), tr("filename cannot be empty"),
                                               QMessageBox::Retry | QMessageBox::Abort, QMessageBox::Retry);
            if (button == QMessageBox::Abort) {
                return;
            }
        } else if (save_filename == source_filepath.fileName() || QFile::exists(save_filepath.toLocalFile())) {
            auto button =
                QMessageBox::warning(this, tr("overwrite source"),
                                     tr("provided filename already exists. Are you sure you want to OVERWRITE it?"),
                                     QMessageBox::Retry | QMessageBox::Abort | QMessageBox::Yes, QMessageBox::Retry);
            switch (button) {
                case QMessageBox::Retry:
                    save_filename = "";
                    break;
                case QMessageBox::Abort:
                    return;
                case QMessageBox::Yes:
                    break;
                default:
                    Q_UNREACHABLE();
            }
        } else {
            break;
        }
    } while (save_filename.isEmpty());
    result_path_ = save_filepath;
    confirm_chaptername_plugin_();
}
void MainWindow::confirm_chaptername_plugin_() {
    QDir plugin_dir = chaptername_plugins_dir_();
    QString plugin = NO_PLUGIN;
    bool confirmed;
    if (plugin_dir.exists()) {
        plugin = QInputDialog::getItem(
            this, tr("select plugin"), tr("chapter name plugins are found. Select one you want to use."),
            chapternames_plugins_(), default_chapternames_plugin_index_(), false, &confirmed);
        if (not confirmed) {
            return;
        }
    }
    if (plugin == NO_PLUGIN) {
        chaptername_plugin_ = std::nullopt;
    } else {
        chaptername_plugin_ = chaptername_plugins_dir_().absoluteFilePath(plugin);
    }
    probe_for_duration_();
}
void MainWindow::probe_for_duration_() {
    QStringList ffprobe_arguments{"-hide_banner", "-show_streams", "-show_format", "-of", "json", "-v", "quiet"};
    QString filename = ui_->listWidget_filenames->item(current_index_)->text();
    process_->start("ffprobe", ffprobe_arguments + QStringList{filename}, false);
    connect(process_, &ProcessWidget::finished, this, impl_::OnTrue([=] { this->register_duration_(); }),
            impl_::ONESHOT_AUTO_CONNECTION);
}
void MainWindow::register_duration_() {
    QString filepath = ui_->listWidget_filenames->item(current_index_)->text();
    QRegularExpression fraction_pattern(R"((\d+)/(\d+))");
    QJsonParseError err;
    auto prove_result = QJsonDocument::fromJson(process_->get_stdout().toUtf8(), &err);
    if (prove_result.isNull()) {
        QMessageBox::critical(this, tr("ffprobe parse error"),
                              tr("failed to parse result of ffprobe\nerror message:%1").arg(err.errorString()));
        return;
    }
    auto duration_str = prove_result.object()["format"].toObject()["duration"].toString();
    bool ok;
    double duration = duration_str.toDouble(&ok);
    if (not ok) {
        QMessageBox::critical(this, tr("ffprobe parse error"), tr("failed to parse duration [%1]").arg(duration_str));
        return;
    }
    concat::VideoInfo info{};
    bool video_found = false, audio_found = false;
    for (auto stream_value : prove_result.object()["streams"].toArray()) {
        auto stream = stream_value.toObject();
        if (stream["codec_type"] == "video") {
            video_found = true;
            info.video_codec = stream["codec_name"].toString();
            info.resolution = QSize(stream["width"].toInt(), stream["height"].toInt());
            auto match = fraction_pattern.match(stream["r_frame_rate"].toString());
            bool ok1, ok2;
            info.framerate = static_cast<double>(match.captured(1).toInt(&ok1)) / match.captured(2).toInt(&ok2);
            if (not(ok1 && ok2)) {
                QMessageBox::critical(this, tr("ffprobe parse error"),
                                      tr("failed to parse frame rate [%1]").arg(stream["r_frame_rate"].toString()));
                return;
            }
            match = fraction_pattern.match(stream["avg_frame_rate"].toString());
            double avg_framerate = static_cast<double>(match.captured(1).toInt(&ok1)) / match.captured(2).toInt(&ok2);
            if (not(ok1 && ok2)) {
                QMessageBox::critical(this, tr("ffprobe parse error"),
                                      tr("failed to parse frame rate [%1]").arg(stream["r_frame_rate"].toString()));
                return;
            }
            info.is_vfr = std::get<double>(info.framerate) != avg_framerate;
            info.video_codec = stream["codec_name"].toString();
        } else if (stream["codec_type"] == "audio") {
            audio_found = true;
            info.audio_codec = stream["codec_name"].toString();
        }
    }
    if (not video_found) {
        QMessageBox::critical(this, tr("ffprobe parse error"), tr("video stream was not found"));
        return;
    }
    if (not audio_found) {
        QMessageBox::critical(this, tr("ffprobe parse error"), tr("audio stream was not found"));
        return;
    }
    current_file_info_ = {filepath, FileInfo::seconds(duration), info, {}};
    tmpfile_paths_.current_src_metadata = tmpdir_->filePath(QStringLiteral("metadata%1.ini").arg(file_infos_.size()));
    retrieve_metadata_(current_file_info_.path, tmpfile_paths_.current_src_metadata, [=] { this->check_metadata_(); });
}
QVector<MainWindow::FileInfo::ChapterInfo> MainWindow::retrieve_chapters_(QString src_filename) {
    QFile src_file(src_filename);
    if (not src_file.open(QFile::ReadOnly | QFile::Text)) {
        throw std::runtime_error(tr("error: failed to open file [%1]").arg(src_filename).toStdString());
    }
    QTextStream src_stream(&src_file);
    auto metadata = src_stream.readAll().split("\n");
    auto metadata_line_iter = metadata.begin();
    QVector<MainWindow::FileInfo::ChapterInfo> result;
    QRegularExpression section_pattern(R"(\[(?<name>.+)\])");
    QRegularExpression keyvalue_pattern("(?<key>[^=]+)=(?<value>.+)");
    bool is_in_chapter_section = false;
    for (; metadata_line_iter != metadata.end(); metadata_line_iter++) {
        auto match = section_pattern.match(*metadata_line_iter);
        if (match.hasMatch()) {
            if (match.captured("name") == "CHAPTER") {
                is_in_chapter_section = true;
                result.push_back({1, 1'000'000'000, 0, 0, ""});
            } else {
                is_in_chapter_section = false;
            }
            continue;
        }
        if (not is_in_chapter_section) {
            continue;
        }
        match = keyvalue_pattern.match(*metadata_line_iter);
        if (not match.hasMatch()) {
            continue;
        }
        auto key = match.captured("key").toUpper();  // ignore case
        auto value = match.captured("value");
        if (key == "TIMEBASE") {
            result.back().timebase_numerator = value.split("/")[0].toInt();
            result.back().timebase_denominator = value.split("/")[1].toInt();
        } else if (key == "START") {
            result.back().start_time = value.toInt();
        } else if (key == "END") {
            result.back().end_time = value.toInt();
        } else if (key == "TITLE") {
            result.back().title = value;
        }
    }
    return result;
}
void MainWindow::check_metadata_() {
    decltype(retrieve_chapters_("")) chapters;
    try {
        chapters = retrieve_chapters_(tmpfile_paths_.current_src_metadata);
    } catch (std::exception &e) {
        QMessageBox::critical(this, tr("error"), QString::fromStdString(e.what()));
        return;
    }
    if (chapters.isEmpty()) {
        create_chapter_();
    } else {
        current_file_info_.chapters = chapters;
        register_file_info_();
    }
}
void MainWindow::create_chapter_() {
    using std::chrono::duration_cast;
    using std::chrono::microseconds;
    current_file_info_.chapters.push_back(
        {1, 1'000'000, 0, duration_cast<microseconds>(current_file_info_.duration).count(), ""});
    QString filename = QUrl::fromLocalFile(ui_->listWidget_filenames->item(current_index_)->text()).fileName();
    if (chaptername_plugin_.has_value()) {
        process_->start(
            PYTHON,
            {chaptername_plugin_.value(), filename, QString::number(current_file_info_.duration.count(), 'g', 10)},
            false);
        connect(process_, &ProcessWidget::finished, this, impl_::OnTrue([=] { this->register_chapter_title_(); }),
                impl_::ONESHOT_AUTO_CONNECTION);
    } else {
        current_file_info_.chapters[0].title = filename;
        register_file_info_();
    }
}
void MainWindow::register_chapter_title_() {
    current_file_info_.chapters[0].title = process_->get_stdout().remove('\n');
    register_file_info_();
}
void MainWindow::register_file_info_() {
    FileInfo::seconds raw_offset(0.0);
    for (const auto &file_info : file_infos_) {
        raw_offset += file_info.duration;
    }
    for (auto &chapter : current_file_info_.chapters) {
        auto timebase = static_cast<double>(chapter.timebase_numerator) / chapter.timebase_denominator;
        qint64 offset = raw_offset.count() / timebase;
        chapter.start_time += offset;
        chapter.end_time += offset;
    }
    file_infos_.push_back(current_file_info_);
    if (current_index_ == ui_->listWidget_filenames->count() - 1) {
        confirm_video_info_();
    } else {
        current_index_++;
        probe_for_duration_();
    }
}
void MainWindow::confirm_video_info_() {
    concat::VideoInfo input_info;
    input_info.audio_codec = QSet<QString>{};
    input_info.video_codec = QSet<QString>{};
    for (const auto &file_info : file_infos_) {
        auto &info = file_info.video_info;
        if (not std::holds_alternative<concat::ValueRange<QSize>>(input_info.resolution)) {  // 最初のイテレーション
            input_info.resolution =
                concat::ValueRange<QSize>{std::get<QSize>(info.resolution), std::get<QSize>(info.resolution)};
        } else {
            auto &current_range = std::get<concat::ValueRange<QSize>>(input_info.resolution);
            auto calc_area = [](const QSize &size) { return size.width() * size.height(); };  // 縦長と横長の区別は妥協
            if (calc_area(std::get<QSize>(info.resolution)) > calc_area(current_range.highest)) {
                current_range.highest = std::get<QSize>(info.resolution);
            } else if (calc_area(std::get<QSize>(info.resolution)) < calc_area(current_range.lowest)) {
                current_range.lowest = std::get<QSize>(info.resolution);
            }
        }
        if (not std::holds_alternative<concat::ValueRange<double>>(input_info.framerate)) {
            input_info.framerate =
                concat::ValueRange<double>{std::get<double>(info.framerate), std::get<double>(info.framerate)};
        } else {
            auto &current_range = std::get<concat::ValueRange<double>>(input_info.framerate);
            if (std::get<double>(info.framerate) > current_range.highest) {
                current_range.highest = std::get<double>(info.framerate);
            } else if (std::get<double>(info.framerate) < current_range.lowest) {
                current_range.lowest = std::get<double>(info.framerate);
            }
        }
        std::get<QSet<QString>>(input_info.audio_codec) += std::get<QString>(info.audio_codec);
        std::get<QSet<QString>>(input_info.video_codec) += std::get<QString>(info.video_codec);
    }
    bool confirmed = false;
    output_video_info_ = VideoInfoDialog::get_video_info(nullptr, tr("video info confirmation"),
                                                         tr("check and edit information about output video"),
                                                         video_info_widget_->info(), input_info, &confirmed);
    if (std::holds_alternative<concat::SameAsHighest<QSize>>(output_video_info_.resolution)) {
        output_video_info_.resolution = std::get<concat::SameAsHighest<QSize>>(output_video_info_.resolution).value;
    }
    if (std::holds_alternative<concat::SameAsLowest<QSize>>(output_video_info_.resolution)) {
        output_video_info_.resolution = std::get<concat::SameAsLowest<QSize>>(output_video_info_.resolution).value;
    }
    if (std::holds_alternative<concat::SameAsHighest<double>>(output_video_info_.framerate)) {
        output_video_info_.framerate = std::get<concat::SameAsHighest<double>>(output_video_info_.framerate).value;
    }
    if (std::holds_alternative<concat::SameAsLowest<double>>(output_video_info_.framerate)) {
        output_video_info_.framerate = std::get<concat::SameAsLowest<double>>(output_video_info_.framerate).value;
    }
    if (std::holds_alternative<concat::SameAsInput<QString>>(output_video_info_.audio_codec)) {
        output_video_info_.audio_codec = std::get<concat::SameAsInput<QString>>(output_video_info_.audio_codec).value;
    }
    if (std::holds_alternative<concat::SameAsInput<QString>>(output_video_info_.video_codec)) {
        output_video_info_.video_codec = std::get<concat::SameAsInput<QString>>(output_video_info_.video_codec).value;
    }
    if (confirmed) {
        confirm_chaptername_();
    }
}
void MainWindow::confirm_chaptername_() {
    bool confirmed;
    QStringList created_chapternames;
    for (const auto &file_info : file_infos_) {
        for (const auto &chapter : file_info.chapters) {
            created_chapternames << chapter.title;
        }
    }
    QStringList confirmed_chapternames =
        ListDialog::get_texts(nullptr, tr("confirm chapternames"),
                              tr("Chapter names of result video will be texts below.The texts are editable."),
                              created_chapternames, &confirmed);
    if (confirmed) {
        auto confirmed_chaptername_iter = confirmed_chapternames.constBegin();
        for (auto &file_info : file_infos_) {
            for (auto &chapter : file_info.chapters) {
                chapter.title = *confirmed_chaptername_iter;
                confirmed_chaptername_iter++;
            }
        }
        concatenate_videos_();
    }
}
namespace impl_ {
int decode_ffmpeg(QStringView, QStringView new_stderr) {
    QRegularExpression time_pattern(R"(time=(?<hours>\d\d):(?<minutes>\d\d):(?<seconds>\d\d).(?<centiseconds>\d\d))");
    auto match = time_pattern.match(new_stderr);
    if (not match.hasMatch()) {
        return -1;
    }
    using std::chrono::duration_cast;
    using std::chrono::hours;
    using std::chrono::minutes;
    using std::chrono::seconds;
    using centiseconds = std::chrono::duration<int, std::centi>;
    using std::chrono::milliseconds;
    return duration_cast<milliseconds>(
               hours(match.captured("hours").toInt()) + minutes(match.captured("minutes").toInt()) +
               seconds(match.captured("seconds").toInt()) + centiseconds(match.captured("centiseconds").toInt()))
        .count();
}
}  // namespace impl_
void MainWindow::concatenate_videos_() {
    if (not tmpdir_->isValid()) {
        QMessageBox::critical(this, tr("temporary directory error"),
                              tr("failed to create temporary directory \n%1").arg(tmpdir_->errorString()));
        return;
    }
    QFile concat_file(tmpdir_->filePath("concat.txt"));
    if (not concat_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(
            this, tr("file open error"),
            tr("failed to open file [%1]. QFile::error(): %2").arg(concat_file.fileName()).arg(concat_file.error()));
        return;
    }
    using std::chrono::duration_cast;
    using milliseconds = std::chrono::duration<int, std::milli>;
    using seconds = std::chrono::duration<double>;
    using namespace std::chrono_literals;
    QTextStream concat_file_stream(&concat_file);
    total_length_ = 0ms;
    for (const auto &file_info : file_infos_) {
        concat_file_stream << "file '" << file_info.path << "'\n";
        total_length_ += duration_cast<milliseconds>(file_info.duration);
    }
    concat_file.close();
    bool resolution_changed = false, audio_codec_changed = false, video_codec_changed = false;
    for (const auto &fileinfo : file_infos_) {
        if (std::get<QSize>(output_video_info_.resolution) != std::get<QSize>(fileinfo.video_info.resolution)) {
            resolution_changed = true;
        }
        if (std::get<QString>(output_video_info_.audio_codec) != std::get<QString>(fileinfo.video_info.audio_codec)) {
            audio_codec_changed = true;
        }
        if (std::get<QString>(output_video_info_.video_codec) != std::get<QString>(fileinfo.video_info.video_codec)) {
            video_codec_changed = true;
        }
    }
    QStringList arguments;
    tmpfile_paths_.concatenated = tmpdir_->filePath("concatenated." + QFileInfo(result_path_.toLocalFile()).suffix());
    // clang-format off
    arguments << "-f" << "concat"
              << "-safe" << "0"
              << output_video_info_.input_file_args
              << "-i" << concat_file.fileName()
              << "-c:a" << (audio_codec_changed? std::get<QString>(output_video_info_.audio_codec) : "copy")
              << "-c:v" << (video_codec_changed? std::get<QString>(output_video_info_.video_codec) : "copy");
    // clang-format on
    if (resolution_changed) {
        auto resolution = std::get<QSize>(output_video_info_.resolution);
        arguments << "-s" << QStringLiteral("%1x%2").arg(resolution.width()).arg(resolution.height());
    }
    arguments += output_video_info_.encoding_args;
    arguments << tmpfile_paths_.concatenated;
    using VT = ProcessWidget::ProgressParams::ValueType;
    auto format = [](VT value) {
        return QTime::fromMSecsSinceStartOfDay(value).toString(tr("hh'h'mm'm'ss's'zzz'ms'"));
    };
    process_->start("ffmpeg", arguments, false,
                    {0, total_length_.count(), impl_::decode_ffmpeg, [format](VT, VT current, VT total) {
                         return QStringLiteral("%1/%2").arg(format(current)).arg(format(total));
                     }});
    tmpfile_paths_.metadata = tmpdir_->filePath("metadata.ini");
    connect(process_, &ProcessWidget::finished, this, impl_::OnTrue([=] {
                this->retrieve_metadata_(this->tmpfile_paths_.concatenated, this->tmpfile_paths_.metadata,
                                         [=] { this->add_chapters_(); });
            }),
            impl_::ONESHOT_AUTO_CONNECTION);
}
void MainWindow::retrieve_metadata_(QString src_filepath, QString dst_filepath, std::function<void(void)> on_success) {
    QStringList arguments;
    // clang-format off
    arguments << "-i" << src_filepath
              << "-f" << "ffmetadata" 
              << dst_filepath;
    // clang-format on
    process_->start("ffmpeg", arguments, false);
    connect(process_, &ProcessWidget::finished, this, impl_::OnTrue(on_success), impl_::ONESHOT_AUTO_CONNECTION);
}
void MainWindow::add_chapters_() {
    QFile metadata_file(tmpfile_paths_.metadata);
    if (not metadata_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QMessageBox::critical(this, tr("file open error"),
                              tr("failed to open file [%1]. QFile::error(): %2")
                                  .arg(metadata_file.fileName())
                                  .arg(metadata_file.error()));
        return;
    }
    QTextStream metadata_stream(&metadata_file);
    metadata_stream << Qt::endl;
    using micro_seconds = std::chrono::duration<double, std::micro>;
    using std::chrono::duration_cast;
    for (const auto &file_info : file_infos_) {
        for (const auto &chapter : file_info.chapters) {
            metadata_stream << "[CHAPTER]" << Qt::endl;
            metadata_stream << "TIMEBASE=" << chapter.timebase_numerator << "/" << chapter.timebase_denominator
                            << Qt::endl;
            metadata_stream << "START=" << chapter.start_time << Qt::endl;
            metadata_stream << "END=" << chapter.end_time << Qt::endl;
            metadata_stream << "TITLE=" << chapter.title << Qt::endl;
        }
    }
    metadata_file.close();

    QStringList arguments;
    // clang-format off
    arguments << "-i" << tmpfile_paths_.concatenated
              << "-i" << tmpfile_paths_.metadata
              << "-map_metadata" << "1"
              << "-map_chapters" << "1"
              << "-c" << "copy" 
              << result_path_.toLocalFile();
    // clang-format on

    // process_->start(
    //     "py", {"-c", "import time;[print(i,flush=True) or time.sleep(1) for i in range(120)]",
    //     tmpfile_paths_.metadata}, true);

    process_->start("ffmpeg", arguments, true, {0, total_length_.count(), impl_::decode_ffmpeg});
    connect(process_, &ProcessWidget::finished, this, impl_::OnTrue([=] { this->cleanup_after_saving_(); }),
            impl_::ONESHOT_AUTO_CONNECTION);
}
void MainWindow::cleanup_after_saving_() {
    delete tmpdir_;
    tmpdir_ = nullptr;
}
void MainWindow::start_saving_() {
    process_ = new ProcessWidget(this, Qt::Window | Qt::CustomizeWindowHint | Qt::WindowMinMaxButtonsHint);
    process_->setWindowModality(Qt::WindowModal);
    process_->setAttribute(Qt::WA_DeleteOnClose, true);
    process_->show();

    if (settings_->contains("temporary_directory_template")) {
        tmpdir_ = new QTemporaryDir(settings_->value("temporary_directory_template").toString());
    } else {
        tmpdir_ = new QTemporaryDir();
    }
    file_infos_.clear();
    current_index_ = 0;
    show_size_();
}
void MainWindow::save_result_() {
    if (ui_->listWidget_filenames->count() == 0) {
        return;
    }
    start_saving_();
}
int MainWindow::default_chapternames_plugin_index_() {
    int result = 0;
    if (settings_->contains("default_chaptername_plugin")) {
        int raw_idx = chapternames_plugins_().indexOf(settings_->value("default_chaptername_plugin"));
        result = qMax(raw_idx, 0);
    }

    return result;
}
QDir MainWindow::chaptername_plugins_dir_() {
    return QDir(QApplication::applicationDirPath() + "/plugins/chapternames");
}
QStringList MainWindow::search_chapternames_plugins_() {
    return chaptername_plugins_dir_().entryList({"*.py"}, QDir::Files);
}

QStringList MainWindow::chapternames_plugins_() { return QStringList{NO_PLUGIN} + search_chapternames_plugins_(); }

void MainWindow::select_default_chaptername_plugin_() {
    QString plugin = NO_PLUGIN;
    bool confirmed;
    plugin = QInputDialog::getItem(this, tr("select plugin"), tr("Select default chapter name plugin."),
                                   chapternames_plugins_(), default_chapternames_plugin_index_(), false, &confirmed);
    if (confirmed && settings_ != nullptr) {
        settings_->setValue("default_chaptername_plugin", plugin);
    }
}

int MainWindow::savefile_name_plugin_index_() {
    int result = 0;
    if (settings_->contains("savefile_name_plugin")) {
        int raw_idx = savefile_name_plugins_().indexOf(settings_->value("savefile_name_plugin"));
        result = qMax(raw_idx, 0);
    }

    return result;
}
QDir MainWindow::savefile_name_plugins_dir_() {
    return QDir(QApplication::applicationDirPath() + "/plugins/savefile_name");
}
QStringList MainWindow::search_savefile_name_plugins_() {
    return savefile_name_plugins_dir_().entryList({"*.py"}, QDir::Files);
}

QStringList MainWindow::savefile_name_plugins_() { return QStringList{NO_PLUGIN} + search_savefile_name_plugins_(); }

void MainWindow::select_savefile_name_plugin_() {
    QString plugin = NO_PLUGIN;
    bool confirmed;
    plugin = QInputDialog::getItem(this, tr("select plugin"), tr("Select savefile name plugin."),
                                   savefile_name_plugins_(), savefile_name_plugin_index_(), false, &confirmed);
    if (confirmed && settings_ != nullptr) {
        settings_->setValue("savefile_name_plugin", plugin);
    }
}