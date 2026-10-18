#pragma once

#include <QAbstractListModel>
#include <QVector>
#include <QString>

namespace zappe {

struct RowData {
    QString title;
    QString service;
    QString serviceLabel;
    QString href;
    QString skillId;
    QString artworkSpec;
    QString artwork;
    QString ambient;
    QString color;
    bool wide = false;
    bool live = false;
    double progress = 0;
};

struct ShelfData {
    QString id;
    QString skillId;
    QString title;
    QString status;
    QString message;
    bool live = false;
    QVector<RowData> rows;
};

struct CatalogFile {
    QString path;
    QVector<ShelfData> shelves;
    bool ok = false;
};

class RowModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount CONSTANT)
public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        ServiceRole,
        HrefRole,
        SkillRole,
        ArtworkRole,
        AmbientRole,
        ColorRole,
        WideRole,
        LiveRole,
        ProgressRole,
    };

    explicit RowModel(QVector<RowData> rows, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    const RowData *at(int row) const;

private:
    QVector<RowData> m_rows;
};

class ShelfModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount CONSTANT)
public:
    enum Roles {
        ShelfIdRole = Qt::UserRole + 1,
        TitleRole,
        StatusRole,
        MessageRole,
        SkillRole,
        WideRole,
        RowsRole,
    };

    explicit ShelfModel(QObject *parent = nullptr);

    void setShelves(QVector<ShelfData> shelves);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    const ShelfData *shelfAt(int row) const;
    RowModel *rowsAt(int row) const;

private:
    struct Entry {
        ShelfData data;
        RowModel *rows = nullptr;
    };
    QVector<Entry> m_shelves;
};

QString resolveCatalogPath(const QString &explicitPath, const QString &dataDir);
CatalogFile loadCatalogFile(const QString &path);
void decorateCatalog(CatalogFile &catalog, const QString &dataDir, const QString &cacheDir);

} // namespace zappe
