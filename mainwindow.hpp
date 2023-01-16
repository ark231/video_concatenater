#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QAudioOutput>
#include <QDir>
#include <QMainWindow>
#include <QMap>
#include <QMediaPlayer>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>
#include <chrono>
#include <optional>
#include <tuple>

#include "processwidget.hpp"
#include "videoinfo.hpp"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

   public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

   private:
    QUrl read_video_dir_cache_();
    void write_video_dir_cache_(QUrl);
    void update_effective_period_of_cache_();

    void open_video_();
    void save_result_();
    void select_default_chaptername_plugin_();
    void select_savefile_name_plugin_();

   private:
    Ui::MainWindow *ui_;
    ProcessWidget *process_ = nullptr;  // deleted on close
    QSettings *settings_ = nullptr;
    struct FileInfo {
        QString path;
        using seconds = std::chrono::duration<double>;
        seconds duration;
        concat::VideoInfo video_info;
        struct ChapterInfo {
            qint32 timebase_numerator;
            qint32 timebase_denominator;
            qint64 start_time;
            qint64 end_time;
            QString title;
        };
        QVector<ChapterInfo> chapters;
    };
    QVector<FileInfo> file_infos_;
    FileInfo current_file_info_;
    concat::VideoInfo output_video_info_;
    std::chrono::duration<int, std::milli> total_length_;
    std::optional<QString> chaptername_plugin_ = std::nullopt;
    static constexpr auto NO_PLUGIN = "do not use any plugins";
#ifdef _WIN32
    static constexpr auto PYTHON = "py";
#else
    static constexpr auto PYTHON = "python";
#endif
    QUrl result_path_;
    struct {
        QString concatenated;
        QString metadata;
        QString current_src_metadata;
    } tmpfile_paths_;
    int current_index_ = 0;
    QTemporaryDir *tmpdir_ = nullptr;

    QDir chaptername_plugins_dir_();
    QStringList search_chapternames_plugins_();
    QStringList chapternames_plugins_();
    int default_chapternames_plugin_index_();

    QDir savefile_name_plugins_dir_();
    QStringList search_savefile_name_plugins_();
    QStringList savefile_name_plugins_();
    int savefile_name_plugin_index_();

    QVector<MainWindow::FileInfo::ChapterInfo> retrieve_chapters_(QString src_filename);

    // steps for creating and saving result
    void start_saving_();
    void show_size_();
    void create_savefile_name_();
    void confirm_savefile_name_();
    void confirm_chaptername_plugin_();
    // iterate through all files
    void probe_for_duration_();
    void register_duration_();
    /* call retrieve_metadata_()*/
    void check_metadata_();
    void create_chapter_();          // called if no chapters are found in metadata
    void register_chapter_title_();  // called if the title of the chapter is generated by plugin
    void register_file_info_();
    // end iteration
    void confirm_video_info_();
    void confirm_chaptername_();
    void concatenate_videos_();
    void retrieve_metadata_(QString src_filepath, QString dst_filepath, std::function<void(void)> on_success);
    void add_chapters_();
    void cleanup_after_saving_();
    // end steps
};
#endif  // MAINWINDOW_H
