#include "vslsamplestate.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 4) { return 2; }
    QFile file(QString::fromLocal8Bit(argv[1])), spec(QString::fromLocal8Bit(argv[2]));
    if (!file.open(QIODevice::ReadOnly) || !spec.open(QIODevice::ReadOnly)) { return 3; }
    const std::string original = file.readAll().toStdString();
    const auto requirements = QJsonDocument::fromJson(spec.readAll()).array();
    std::vector<std::vector<int>> paths;
    std::vector<std::string> entries;
    for (const auto& value : requirements) {
        const auto object = value.toObject();
        std::vector<int> path;
        for (const auto& index : object["path"].toArray()) { path.push_back(index.toInt()); }
        paths.push_back(path);
        entries.push_back(object["patchEntry"].toString().toStdString());
    }
    int checks = 0;
    auto check = [&checks](bool ok) { ++checks; if (!ok) { std::cerr << "FAILED " << checks << '\n'; std::exit(1); } };
    auto result = muse::vst::enableVslSamples(original, paths, entries);
    check(result.valid && result.changed && result.enabledCount == paths.size());
    QFile output(QString::fromLocal8Bit(argv[3]));
    check(output.open(QIODevice::WriteOnly));
    check(output.write(result.state.data(), result.state.size()) == static_cast<qint64>(result.state.size()));
    output.close();
    const auto noOp = muse::vst::enableVslSamples(result.state, paths, entries);
    check(noOp.valid && !noOp.changed && noOp.state == result.state && noOp.enabledCount == 0);
    const auto noRequirements = muse::vst::enableVslSamples(original, {}, {});
    check(noRequirements.valid && noRequirements.state == original && !noRequirements.changed);
    auto expectReject = [&](const std::string& state, const auto& p, const auto& e) {
        const auto bad = muse::vst::enableVslSamples(state, p, e);
        check(!bad.valid && !bad.changed && bad.enabledCount == 0 && bad.state == state && !bad.error.empty());
    };
    expectReject("bad-state", paths, entries);
    expectReject(original.substr(0, original.size() - 1), paths, entries);
    auto wrongEntry = entries; wrongEntry.back() += "-wrong";
    expectReject(original, paths, wrongEntry); // atomic, even if preceding paths were valid
    auto wrongPaths = paths; wrongPaths.back()[0] = 99999;
    expectReject(original, wrongPaths, entries);
    wrongPaths.back()[0] = -1;
    expectReject(original, wrongPaths, entries);
    wrongPaths.back().clear();
    expectReject(original, wrongPaths, entries);
    expectReject(original, paths, std::vector<std::string>());
    auto corrupt = original; corrupt[8] ^= 0x7f;
    expectReject(corrupt, paths, entries);
    auto duplicatePaths = paths; duplicatePaths.push_back(paths[0]);
    auto duplicateEntries = entries; duplicateEntries.push_back(entries[0]);
    const auto dedup = muse::vst::enableVslSamples(original, duplicatePaths, duplicateEntries);
    check(dedup.valid && dedup.enabledCount == paths.size());
    std::cout << "PASS " << checks << " checks; enabled " << result.enabledCount << " slots\n";
    return 0;
}
