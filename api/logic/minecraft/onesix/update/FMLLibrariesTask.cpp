#include "Env.h"
#include <FileSystem.h>
#include <minecraft/VersionFilterData.h>
#include "FMLLibrariesTask.h"
#include "minecraft/onesix/OneSixInstance.h"


FMLLibrariesTask::FMLLibrariesTask(OneSixInstance * inst)
{
	m_inst = inst;
}
void FMLLibrariesTask::executeTask()
{
	// Get the mod list
	OneSixInstance *inst = (OneSixInstance *)m_inst;
	std::shared_ptr<MinecraftProfile> profile = inst->getMinecraftProfile();
	bool forge_present = false;

	if (!profile->hasTrait("legacyFML"))
	{
		emitSucceeded();
		return;
	}

	QString version = inst->intendedVersionId();
	auto &fmlLibsMapping = g_VersionFilterData.fmlLibsMapping;
	if (!fmlLibsMapping.contains(version))
	{
		emitSucceeded();
		return;
	}

	auto &libList = fmlLibsMapping[version];

	// determine if we need some libs for FML or forge
	setStatus(tr("Checking for FML libraries..."));
	forge_present = (profile->versionPatch("net.minecraftforge") != nullptr);
	// we don't...
	if (!forge_present)
	{
		emitSucceeded();
		return;
	}

	// now check the lib folder inside the instance for files.
	for (auto &lib : libList)
	{
		QFileInfo libInfo(FS::PathCombine(inst->FMLlibDir(), lib.filename));
		if (libInfo.exists())
			continue;
		fmlLibsToProcess.append(lib);
	}

	// if everything is in place, there's nothing to do here...
	if (fmlLibsToProcess.isEmpty())
	{
		emitSucceeded();
		return;
	}

	// download missing libs to our place
	setStatus(tr("Dowloading FML libraries..."));
	auto dljob = new NetJob("FML libraries");
	auto metacache = ENV.metacache();
	for (auto &lib : fmlLibsToProcess)
	{
		auto entry = metacache->resolveEntry("fmllibs", lib.filename);
		QString urlString = lib.ours ? URLConstants::FMLLIBS_OUR_BASE_URL + lib.filename
									: URLConstants::FMLLIBS_FORGE_BASE_URL + lib.filename;
		dljob->addNetAction(Net::Download::makeCached(QUrl(urlString), entry));
	}

	connect(dljob, &NetJob::succeeded, this, &FMLLibrariesTask::fmllibsFinished);
	connect(dljob, &NetJob::failed, this, &FMLLibrariesTask::fmllibsFailed);
	connect(dljob, &NetJob::progress, this, &FMLLibrariesTask::progress);
	downloadJob.reset(dljob);
	downloadJob->start();
}

bool FMLLibrariesTask::canAbort() const
{
	return true;
}

void FMLLibrariesTask::fmllibsFinished()
{
	downloadJob.reset();
	if (!fmlLibsToProcess.isEmpty())
	{
		setStatus(tr("Copying FML libraries into the instance..."));
		OneSixInstance *inst = (OneSixInstance *)m_inst;
		auto metacache = ENV.metacache();
		int index = 0;
		for (auto &lib : fmlLibsToProcess)
		{
			progress(index, fmlLibsToProcess.size());
			auto entry = metacache->resolveEntry("fmllibs", lib.filename);
			auto path = FS::PathCombine(inst->FMLlibDir(), lib.filename);
			if (!FS::ensureFilePathExists(path))
			{
				emitFailed(tr("Failed creating FML library folder inside the instance."));
				return;
			}
			if (!QFile::copy(entry->getFullPath(), FS::PathCombine(inst->FMLlibDir(), lib.filename)))
			{
				emitFailed(tr("Failed copying Forge/FML library: %1.").arg(lib.filename));
				return;
			}
			index++;
		}
		progress(index, fmlLibsToProcess.size());
	}
	emitSucceeded();
}
void FMLLibrariesTask::fmllibsFailed(QString reason)
{
	QStringList failed = downloadJob->getFailedFiles();
	QString failed_all = failed.join("\n");
	emitFailed(tr("Failed to download the following files:\n%1\n\nReason:%2\nPlease try again.").arg(failed_all, reason));
}

bool FMLLibrariesTask::abort()
{
	if(downloadJob)
	{
		return downloadJob->abort();
	}
	else
	{
		qWarning() << "Prematurely aborted FMLLibrariesTask";
	}
	return true;
}
