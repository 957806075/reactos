
#include "../../pch.h"

#include "mingw.h"
#include <assert.h>

using std::string;
using std::vector;

static class MingwFactory : public Backend::Factory
{
public:
	MingwFactory() : Factory ( "mingw" ) {}
	Backend* operator() ( Project& project )
	{
		return new MingwBackend ( project );
	}
} factory;


MingwBackend::MingwBackend ( Project& project )
	: Backend ( project )
{
}

void
MingwBackend::Process ()
{
	DetectPCHSupport();

	CreateMakefile ();
	GenerateHeader ();
	GenerateGlobalVariables ();
	GenerateAllTarget ();
	GenerateInitTarget ();
	GenerateXmlBuildFilesMacro();
	for ( size_t i = 0; i < ProjectNode.modules.size (); i++ )
	{
		Module& module = *ProjectNode.modules[i];
		ProcessModule ( module );
	}
	CheckAutomaticDependencies ();
	CloseMakefile ();
}

void
MingwBackend::CreateMakefile ()
{
	fMakefile = fopen ( ProjectNode.makefile.c_str (), "w" );
	if ( !fMakefile )
		throw AccessDeniedException ( ProjectNode.makefile );
	MingwModuleHandler::SetMakefile ( fMakefile );
	MingwModuleHandler::SetUsePch ( use_pch );
}

void
MingwBackend::CloseMakefile () const
{
	if (fMakefile)
		fclose ( fMakefile );
}

void
MingwBackend::GenerateHeader () const
{
	fprintf ( fMakefile, "# THIS FILE IS AUTOMATICALLY GENERATED, EDIT 'ReactOS.xml' INSTEAD\n\n" );
}

void
MingwBackend::GenerateProjectCFlagsMacro ( const char* assignmentOperation,
                                           IfableData& data ) const
{
	size_t i;

	fprintf (
		fMakefile,
		"PROJECT_CFLAGS %s",
		assignmentOperation );
	for ( i = 0; i < data.includes.size(); i++ )
	{
		fprintf (
			fMakefile,
			" -I%s",
			data.includes[i]->directory.c_str() );
	}

	for ( i = 0; i < data.defines.size(); i++ )
	{
		Define& d = *data.defines[i];
		fprintf (
			fMakefile,
			" -D%s",
			d.name.c_str() );
		if ( d.value.size() )
			fprintf (
				fMakefile,
				"=%s",
				d.value.c_str() );
	}
	fprintf ( fMakefile, "\n" );
}

void
MingwBackend::GenerateGlobalCFlagsAndProperties (
	const char* assignmentOperation,
	IfableData& data ) const
{
	size_t i;

	for ( i = 0; i < data.properties.size(); i++ )
	{
		Property& prop = *data.properties[i];
		fprintf ( fMakefile, "%s := %s\n",
			prop.name.c_str(),
			prop.value.c_str() );
	}

	if ( data.includes.size() || data.defines.size() )
	{
		GenerateProjectCFlagsMacro ( assignmentOperation,
                                     data );
	}

	for ( i = 0; i < data.ifs.size(); i++ )
	{
		If& rIf = *data.ifs[i];
		if ( rIf.data.defines.size()
			|| rIf.data.includes.size()
			|| rIf.data.ifs.size() )
		{
			fprintf (
				fMakefile,
				"ifeq (\"$(%s)\",\"%s\")\n",
				rIf.property.c_str(),
				rIf.value.c_str() );
			GenerateGlobalCFlagsAndProperties (
				"+=",
				rIf.data );
			fprintf (
				fMakefile,
				"endif\n\n" );
		}
	}
}

string
MingwBackend::GenerateProjectLFLAGS () const
{
	string lflags;
	for ( size_t i = 0; i < ProjectNode.linkerFlags.size (); i++ )
	{
		LinkerFlag& linkerFlag = *ProjectNode.linkerFlags[i];
		if ( lflags.length () > 0 )
			lflags += " ";
		lflags += linkerFlag.flag;
	}
	return lflags;
}

void
MingwBackend::GenerateGlobalVariables () const
{
#define TOOL_PREFIX "$(Q)." SSEP "tools" SSEP
	fprintf ( fMakefile, "mkdir = " TOOL_PREFIX "rmkdir" EXEPOSTFIX "\n" );
	fprintf ( fMakefile, "winebuild = " TOOL_PREFIX "winebuild" SSEP "winebuild" EXEPOSTFIX "\n" );
	fprintf ( fMakefile, "bin2res = " TOOL_PREFIX "bin2res" SSEP "bin2res" EXEPOSTFIX "\n" );
	fprintf ( fMakefile, "cabman = " TOOL_PREFIX "cabman" SSEP "cabman" EXEPOSTFIX "\n" );
	fprintf ( fMakefile, "cdmake = " TOOL_PREFIX "cdmake" SSEP "cdmake" EXEPOSTFIX "\n" );
	fprintf ( fMakefile, "rsym = " TOOL_PREFIX "rsym" EXEPOSTFIX "\n" );
	fprintf ( fMakefile, "wrc = " TOOL_PREFIX "wrc" SSEP "wrc" EXEPOSTFIX "\n" );
	fprintf ( fMakefile, "\n" );
	GenerateGlobalCFlagsAndProperties (
		"=",
		ProjectNode.non_if_data );
	fprintf ( fMakefile, "PROJECT_RCFLAGS = $(PROJECT_CFLAGS)\n" );
	fprintf ( fMakefile, "PROJECT_LFLAGS = %s\n",
	          GenerateProjectLFLAGS ().c_str () );
	fprintf ( fMakefile, "\n" );
}

bool
MingwBackend::IncludeInAllTarget ( const Module& module ) const
{
	if ( module.type == ObjectLibrary )
		return false;
	if ( module.type == BootSector )
		return false;
	if ( module.type == Iso )
		return false;
	return true;
}

void
MingwBackend::GenerateAllTarget () const
{
	fprintf ( fMakefile, "all:" );
	int wrap_count = 0;
	for ( size_t i = 0; i < ProjectNode.modules.size (); i++ )
	{
		Module& module = *ProjectNode.modules[i];
		if ( IncludeInAllTarget ( module ) )
		{
			if ( wrap_count++ == 5 )
				fprintf ( fMakefile, " \\\n\t\t" ), wrap_count = 0;
			fprintf ( fMakefile,
			          " %s",
			          FixupTargetFilename ( module.GetPath () ).c_str () );
		}
	}
	fprintf ( fMakefile, "\n\t\n\n" );
}

string
MingwBackend::GetBuildToolDependencies () const
{
	string dependencies;
	for ( size_t i = 0; i < ProjectNode.modules.size (); i++ )
	{
		Module& module = *ProjectNode.modules[i];
		if ( module.type == BuildTool )
		{
			if ( dependencies.length () > 0 )
				dependencies += " ";
			dependencies += module.GetDependencyPath ();
		}
	}
	return dependencies;
}

void
MingwBackend::GenerateInitTarget () const
{
	fprintf ( fMakefile,
	          "init:");
	fprintf ( fMakefile,
	          " $(ROS_INTERMEDIATE)." SSEP "tools" );
	fprintf ( fMakefile,
	          " %s",
	          GetBuildToolDependencies ().c_str () );
	fprintf ( fMakefile,
	          " %s",
	          "include" SSEP "reactos" SSEP "buildno.h" );
	fprintf ( fMakefile,
	          "\n\t\n\n" );

	fprintf ( fMakefile,
	          "$(ROS_INTERMEDIATE)." SSEP "tools:\n" );
	fprintf ( fMakefile,
	          "ifneq ($(ROS_INTERMEDIATE),)\n" );
	fprintf ( fMakefile,
	          "\t${nmkdir} $(ROS_INTERMEDIATE)\n" );
	fprintf ( fMakefile,
	          "endif\n" );
	fprintf ( fMakefile,
	          "\t${nmkdir} $(ROS_INTERMEDIATE)." SSEP "tools\n" );
	fprintf ( fMakefile,
	          "\n" );
}

void
MingwBackend::GenerateXmlBuildFilesMacro() const
{
	fprintf ( fMakefile,
	          "XMLBUILDFILES = %s \\\n",
	          ProjectNode.GetProjectFilename ().c_str () );
	string xmlbuildFilenames;
	int numberOfExistingFiles = 0;
	for ( size_t i = 0; i < ProjectNode.xmlbuildfiles.size (); i++ )
	{
		XMLInclude& xmlbuildfile = *ProjectNode.xmlbuildfiles[i];
		if ( !xmlbuildfile.fileExists )
			continue;
		numberOfExistingFiles++;
		if ( xmlbuildFilenames.length () > 0 )
			xmlbuildFilenames += " ";
		xmlbuildFilenames += NormalizeFilename ( xmlbuildfile.topIncludeFilename );
		if ( numberOfExistingFiles % 5 == 4 || i == ProjectNode.xmlbuildfiles.size () - 1 )
		{
			fprintf ( fMakefile,
			          "\t%s",
			          xmlbuildFilenames.c_str ());
			if ( i == ProjectNode.xmlbuildfiles.size () - 1 )
			{
				fprintf ( fMakefile,
				          "\n" );
			}
			else
			{
				fprintf ( fMakefile,
				          " \\\n",
				          xmlbuildFilenames.c_str () );
			}
			xmlbuildFilenames.resize ( 0 );
		}
		numberOfExistingFiles++;
	}
	fprintf ( fMakefile,
	          "\n" );
}

void
MingwBackend::CheckAutomaticDependencies ()
{
	AutomaticDependency automaticDependency ( ProjectNode );
	automaticDependency.Process ();
	automaticDependency.CheckAutomaticDependencies ();
}

void
MingwBackend::ProcessModule ( Module& module ) const
{
	MingwModuleHandler* h = MingwModuleHandler::LookupHandler (
		module.node.location,
		module.type );
	MingwModuleHandler::string_list clean_files;
	if ( module.host == HostDefault )
	{
		module.host = h->DefaultHost();
		assert ( module.host != HostDefault );
	}
	h->Process ( module, clean_files );
	h->GenerateCleanTarget ( module, clean_files );
	h->GenerateDirectoryTargets ();
}

string
FixupTargetFilename ( const string& targetFilename )
{
	return string("$(ROS_INTERMEDIATE)") + NormalizeFilename ( targetFilename );
}

void
MingwBackend::DetectPCHSupport()
{
	string path = "tools" SSEP "rbuild" SSEP "backend" SSEP "mingw" SSEP "pch_detection.h";
	system ( ssprintf("gcc -c %s", path.c_str()).c_str() );
	path += ".gch";

	FILE* f = fopen ( path.c_str(), "rb" );
	if ( f )
	{
		use_pch = true;
		fclose(f);
		unlink ( path.c_str() );
	}
	else
		use_pch = false;

	// TODO FIXME - eventually check for ROS_USE_PCH env var and
	// allow that to override use_pch if true
}
