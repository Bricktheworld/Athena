using Sharpmake;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace Athena
{
  abstract class AthenaProject : Project
  {
    public AthenaProject()
    {
      AddTargets(
        new Target(
          Platform.win64,
          DevEnv.vs2022,
          Optimization.Debug | Optimization.Release
        )
      );
    }

    public string GetPath(string relative_path)
    {
      return SharpmakeCsPath + @"\" + relative_path;
    }

    public string GetSourcePath(string relative_path)
    {
      return SourceRootPath + @"\" + relative_path;
    }

    [Configure]
    public virtual void ConfigureAll(Project.Configuration conf, Target target)
    {
      conf.Name = @"[target.Optimization]";
      conf.ProjectPath = @"[project.SharpmakeCsPath]\VS\[project.Name]";
      conf.Options.Add(Options.Vc.Compiler.CppLanguageStandard.CPP20);
      conf.Options.Add(Options.Vc.Compiler.Exceptions.Disable);
      conf.Options.Add(Options.Vc.Linker.CreateHotPatchableImage.Enable);
      conf.Options.Add(Options.Vc.Linker.EnableCOMDATFolding.DoNotRemoveRedundantCOMDATs);
      conf.Options.Add(Options.Vc.Linker.GenerateDebugInformation.Enable);
      conf.Options.Add(Options.Vc.General.ExternalWarningLevel.Level0);
      conf.Options.Add(Options.Vc.General.TreatAngleIncludeAsExternal.Enable);
      conf.Options.Add(
        new Options.Vc.Compiler.DisableSpecificWarnings(
          "4201", // Nonstandard extension used: nameless struct/union
          "5054", // Deprecated & between enumrations of different types
          "4530"  // TODO(bshihabi): Exception handler used
        )
      );
      if (target.Optimization == Optimization.Debug)
      {
        conf.Options.Add(Options.Vc.Compiler.RuntimeLibrary.MultiThreadedDebugDLL);
      }
      else if (target.Optimization == Optimization.Release)
      {
        conf.Options.Add(Options.Vc.Compiler.RuntimeLibrary.MultiThreadedDLL);
      }
      conf.Options.Add(Options.Vc.Linker.Reference.KeepUnreferencedData);
      conf.Defines.Add("UNICODE");
      conf.VcxprojUserFile = new Configuration.VcxprojUserFileSettings();
      conf.VcxprojUserFile.LocalDebuggerWorkingDirectory = @"[project.SharpmakeCsPath]";
    }
  }

  abstract class AthenaRuntimeProject : AthenaProject
  {
    public Strings BuiltShaderHeaders;
    public AthenaRuntimeProject()
    {
      BuiltShaderHeaders = new Strings();
    }

    [Configure]
    public override void ConfigureAll(Project.Configuration conf, Target target)
    {
      base.ConfigureAll(conf, target);
      conf.Options.Add(Options.Vc.General.TreatWarningsAsErrors.Enable);
    }
  }

  abstract class AthenaToolProject : AthenaProject
  {
    [Configure]
    public override void ConfigureAll(Project.Configuration conf, Target target)
    {
      base.ConfigureAll(conf, target);
      conf.IncludePaths.Add(@"[project.SourceRootPath]\..\Vendor");
    }
  }

  abstract class AthenaToolQtProject : AthenaToolProject
  {
    [Configure]
    public override void ConfigureAll(Project.Configuration conf, Target target)
    {
      base.ConfigureAll(conf, target);
      conf.LibraryFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6BundledFreetype.lib");
      conf.LibraryFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6BundledLibjpeg.lib");
      conf.LibraryFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6BundledLibpng.lib");
      conf.LibraryFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6Concurrent.lib");
      conf.LibraryFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6Core.lib");
      conf.LibraryFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6DBus.lib");
      conf.LibraryFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6DeviceDiscoverySupport.lib");
      conf.LibraryFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6EntryPoint.lib");
      conf.LibraryFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6ExampleIcons.lib");
      conf.LibraryFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6ExamplesAssetDownloader.lib");
      conf.LibraryFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6FbSupport.lib");
      conf.LibraryFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6Gui.lib");
      conf.LibraryFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6OpenGL.lib");
      conf.LibraryFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6OpenGLWidgets.lib");
      conf.LibraryFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6PrintSupport.lib");
      conf.LibraryFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6Sql.lib");
      conf.LibraryFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6Test.lib");
      conf.LibraryFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6Widgets.lib");
      conf.LibraryFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6Xml.lib");

      conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6Concurrent.dll");
      conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6Core.dll");
      conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6DBus.dll");
      conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6Gui.dll");
      conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6Network.dll");
      conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6OpenGL.dll");
      conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6OpenGLWidgets.dll");
      conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6PrintSupport.dll");
      conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6Sql.dll");
      conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6Test.dll");
      conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6Widgets.dll");
      conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\..\Lib\Qt6\Qt6Xml.dll");
      conf.TargetCopyFilesToSubDirectory.Add(new KeyValuePair<string, string>(@"[project.SourceRootPath]\..\Lib\Qt6\generic\qtuiotouchplugin.dll", @"generic"));
      conf.TargetCopyFilesToSubDirectory.Add(new KeyValuePair<string, string>(@"[project.SourceRootPath]\..\Lib\Qt6\imageformats\qgif.dll", @"imageformats"));
      conf.TargetCopyFilesToSubDirectory.Add(new KeyValuePair<string, string>(@"[project.SourceRootPath]\..\Lib\Qt6\imageformats\qico.dll", @"imageformats"));
      conf.TargetCopyFilesToSubDirectory.Add(new KeyValuePair<string, string>(@"[project.SourceRootPath]\..\Lib\Qt6\imageformats\qjpeg.dll", @"imageformats"));
      conf.TargetCopyFilesToSubDirectory.Add(new KeyValuePair<string, string>(@"[project.SourceRootPath]\..\Lib\Qt6\networkinformation\qnetworklistmanager.dll", @"networkinformation"));
      conf.TargetCopyFilesToSubDirectory.Add(new KeyValuePair<string, string>(@"[project.SourceRootPath]\..\Lib\Qt6\platforms\qdirect2d.dll", @"platforms"));
      conf.TargetCopyFilesToSubDirectory.Add(new KeyValuePair<string, string>(@"[project.SourceRootPath]\..\Lib\Qt6\platforms\qminimal.dll", @"platforms"));
      conf.TargetCopyFilesToSubDirectory.Add(new KeyValuePair<string, string>(@"[project.SourceRootPath]\..\Lib\Qt6\platforms\qoffscreen.dll", @"platforms"));
      conf.TargetCopyFilesToSubDirectory.Add(new KeyValuePair<string, string>(@"[project.SourceRootPath]\..\Lib\Qt6\platforms\qwindows.dll", @"platforms"));
      conf.TargetCopyFilesToSubDirectory.Add(new KeyValuePair<string, string>(@"[project.SourceRootPath]\..\Lib\Qt6\sqldrivers\qsqlite.dll", @"sqldrivers"));
      conf.TargetCopyFilesToSubDirectory.Add(new KeyValuePair<string, string>(@"[project.SourceRootPath]\..\Lib\Qt6\sqldrivers\qsqlodbc.dll", @"sqldrivers"));
      conf.TargetCopyFilesToSubDirectory.Add(new KeyValuePair<string, string>(@"[project.SourceRootPath]\..\Lib\Qt6\styles\qmodernwindowsstyle.dll", @"styles"));
      conf.TargetCopyFilesToSubDirectory.Add(new KeyValuePair<string, string>(@"[project.SourceRootPath]\..\Lib\Qt6\tls\qcertonlybackend.dll", @"tls"));
      conf.TargetCopyFilesToSubDirectory.Add(new KeyValuePair<string, string>(@"[project.SourceRootPath]\..\Lib\Qt6\tls\qschannelbackend.dll", @"tls"));
    }
  }

  [Sharpmake.Generate]
  class CustomBuildShaderFile : Project.Configuration.CustomFileBuildStep
  {
    public CustomBuildShaderFile(AthenaRuntimeProject project, string src, string dst)
    {
      Strings hlsli_files = new Strings(project.ResolvedSourceFiles.Where(file => Regex.IsMatch(file, @".*\.hlsli$")));
      string output_full = project.GetSourcePath(dst);
      KeyInput = src;
      Output = output_full;

      string python_path = project.GetPath(@"Bin\python-3.12.0\python.exe");
      string compile_shaders_py_path = project.GetPath(@"Code\Core\Scripts\compile_shader.py");
      string dxc_path = project.GetPath(@"Code\Core\Bin\dxc\dxc.exe");
      Executable = python_path;
      ExecutableArguments = string.Format(@"{0} {1} -o {2} --path_to_dxc {3}", compile_shaders_py_path, src, output_full, dxc_path);
      Description = string.Format("Compiling shader {0}", src);
      AdditionalInputs = hlsli_files;
    }

    public static void AddFilesExt(Project project)
    {
      project.SourceFilesExtensions.Add(".csh");
      project.SourceFilesExtensions.Add(".psh");
      project.SourceFilesExtensions.Add(".vsh");
      project.SourceFilesExtensions.Add(".rtsh");
    }

    public static void ClaimShaderFiles(AthenaRuntimeProject project)
    {
      Strings hlsl_files = new Strings(project.ResolvedSourceFiles.Where(file => Regex.IsMatch(file, @".*\.[vpcrtm]+sh$")));
      foreach (string file in hlsl_files)
      {
        string base_name = Path.GetFileNameWithoutExtension(file);
        var output = string.Format(@"Generated\Shaders\{0}.built.h", Path.GetFileName(file));

        var hlsl_compile_task = new CustomBuildShaderFile(project, file, output);
        project.BuiltShaderHeaders.Add(project.GetSourcePath(output));
        foreach (Project.Configuration conf in project.Configurations)
        {
          string target_name = conf.Target.Name;
          conf.CustomFileBuildSteps.Add(hlsl_compile_task);
        }
      }

    }
  }

  [Sharpmake.Generate]
  class FoundationProject : AthenaProject
  {
    public FoundationProject()
    {
      Name = "Foundation";
      SourceRootPath = @"[project.SharpmakeCsPath]\Code\Core\Foundation";
    }

    [Configure]
    public override void ConfigureAll(Configuration conf, Target target)
    {
      base.ConfigureAll(conf, target);
      conf.Defines.Add("FOUNDATION_EXPORT");
      conf.IncludePaths.Add(@"[project.SourceRootPath]\..\..\");
      conf.Options.Add(Options.Vc.General.TreatWarningsAsErrors.Enable);
      conf.Output = Configuration.OutputType.Dll;
    }
  }

  [Sharpmake.Generate]
  class EngineProject : AthenaRuntimeProject
  {
    public string ShaderTableHeader
    {
      get => @"Generated\shader_table.h";
    }

    public string ShaderTableSource
    {
      get => @"Generated\shader_table.cpp";
    }
    public EngineProject()
    {
      Name = "Engine";
      SourceRootPath = @"[project.SharpmakeCsPath]\Code\Core\Engine";
      SourceFilesExtensions.Add(".py");
      SourceFilesExtensions.Add(".hlsli");
      SourceFiles.Add(ShaderTableHeader);
      SourceFiles.Add(ShaderTableSource);
      CustomBuildShaderFile.AddFilesExt(this);
    }

    protected override void ExcludeOutputFiles()
    {
      base.ExcludeOutputFiles();
      CustomBuildShaderFile.ClaimShaderFiles(this);

      string python_path = GetPath(@"Bin\python-3.12.0\python.exe");
      string generate_shader_table_py_path = GetPath(@"Code\Core\Scripts\generate_shader_table.py");

      var shader_srcs = new Strings(ResolvedSourceFiles.Where(file => Regex.IsMatch(file, @".*\.[vpcrtm]+sh$")));

      var shader_table_header_build = new Configuration.CustomFileBuildStep
      {
        KeyInput = shader_srcs.ElementAt(0),
        Output = GetSourcePath(ShaderTableHeader),
        Description = "",
        Executable = python_path,
        ExecutableArguments = string.Format(@"{0} --output_header {1} --output_source {2} --inputs {3}", generate_shader_table_py_path, GetSourcePath(ShaderTableHeader), GetSourcePath(ShaderTableSource), BuiltShaderHeaders.JoinStrings(" ")),
        AdditionalInputs = shader_srcs,
      };
      foreach (string header in BuiltShaderHeaders)
      {
        shader_table_header_build.AdditionalInputs.Add(header);
      }


      foreach (Project.Configuration conf in Configurations)
      {
        conf.CustomFileBuildSteps.Add(shader_table_header_build);
      }
    }

    [Configure]
    public override void ConfigureAll(Configuration conf, Target target)
    {
      base.ConfigureAll(conf, target);
      conf.Output = Configuration.OutputType.Exe;
      conf.AddPublicDependency<FoundationProject>(target);
      if (target.Optimization == Optimization.Debug)
      {
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\DirectXTK.lib");
      }
      else if (target.Optimization == Optimization.Release)
      {
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\DirectXTK.lib");
      }
      conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\D3D12Core.dll");
      conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\D3D12Core.pdb");
      conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\D3D12SDKLayers.dll");
      conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\D3D12SDKLayers.pdb");

      conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\dstorage.lib");
      conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\dstorage.dll");
      conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\dstoragecore.dll");

      conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\WinPixEventRuntime.lib");
      conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\WinPixEventRuntime.dll");

      conf.Options.Add(Options.Vc.Linker.SubSystem.Windows);
    }
  }

  [Sharpmake.Generate]
  class AssetBuilderProject : AthenaToolProject
  {
    public AssetBuilderProject()
    {
      Name = "AssetBuilder";
      SourceRootPath = @"[project.SharpmakeCsPath]\Code\Core\Tools\AssetBuilder";
    }

    public override void ConfigureAll(Configuration conf, Target target)
    {
      base.ConfigureAll(conf, target);
      conf.Output = Configuration.OutputType.Exe;
      conf.AddPublicDependency<FoundationProject>(target);
      conf.IncludePaths.Add(@"[project.SourceRootPath]\Vendor");
      if (target.Optimization == Optimization.Debug)
      {
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\DirectXMesh.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\assimp-vc143-mtd.lib");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\assimp-vc143-mtd.dll");
      }
      else if (target.Optimization == Optimization.Release)
      {
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\DirectXMesh.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\assimp-vc143-mtd.lib");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\assimp-vc143-mtd.dll");
      }
    }
  }

  [Sharpmake.Generate]
  class UsdBuilderProject : AthenaToolProject
  {
    public UsdBuilderProject()
    {
      Name = "UsdBuilder";
      SourceRootPath = @"[project.SharpmakeCsPath]\Code\Core\Tools\UsdBuilder";
    }

    public override void ConfigureAll(Configuration conf, Target target)
    {
      base.ConfigureAll(conf, target);
      conf.Output = Configuration.OutputType.Exe;
      conf.AddPublicDependency<FoundationProject>(target);
      conf.IncludePaths.Add(@"[project.SourceRootPath]\Vendor");
      conf.IncludePaths.Add(@"[project.SourceRootPath]\Vendor\TinyUsd");
      var copy_usd_folder = new Configuration.BuildStepCopy(
        @"[project.SourceRootPath]\Lib\usd",
        @"output\[target.Platform]\[target.Optimization]\usd"
      );
      copy_usd_folder.Mirror = true;
      conf.EventPostBuildExe.Add(copy_usd_folder);
      if (target.Optimization == Optimization.Debug)
      {
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\MaterialXCore.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\MaterialXFormat.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\MaterialXGenGlsl.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\MaterialXGenMdl.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\MaterialXGenMsl.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\MaterialXGenOsl.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\MaterialXGenShader.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\MaterialXRender.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\MaterialXRenderGlsl.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\MaterialXRenderHw.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\MaterialXRenderOsl.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\osdCPU.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\osdGPU.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbb.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbbbind.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbbbind_debug.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbbmalloc.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbbmalloc_debug.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbbmalloc_proxy.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbbmalloc_proxy_debug.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbbproxy.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbbproxy_debug.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbb_debug.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbb_preview.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbb_preview_debug.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_ar.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_arch.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_cameraUtil.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_garch.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_geomUtil.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_gf.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_glf.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hd.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hdar.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hdGp.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hdMtlx.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hdsi.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hdSt.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hdx.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hf.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hgi.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hgiGL.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hgiInterop.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hio.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_js.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_kind.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_ndr.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_pcp.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_pegtl.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_plug.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_pxOsd.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_sdf.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_sdr.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_tf.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_trace.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_ts.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usd.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdAppUtils.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdBakeMtlx.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdGeom.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdHydra.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdImaging.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdImagingGL.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdLux.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdMedia.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdMtlx.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdPhysics.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdProc.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdProcImaging.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdRender.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdRi.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdRiPxrImaging.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdSemantics.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdShade.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdSkel.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdSkelImaging.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdUI.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdUtils.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdVol.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdVolImaging.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_vt.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_work.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\zlibd.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Debug\zlibstaticd.lib");

        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_ar.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_arch.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_cameraUtil.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_garch.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_geomUtil.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_gf.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_glf.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hd.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hdar.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hdGp.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hdMtlx.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hdsi.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hdSt.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hdx.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hf.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hgi.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hgiGL.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hgiInterop.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hio.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_js.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_kind.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_ndr.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_pcp.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_pegtl.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_plug.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_pxOsd.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_sdf.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_sdr.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_tf.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_trace.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_ts.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usd.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdAppUtils.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdBakeMtlx.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdGeom.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdHydra.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdImaging.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdImagingGL.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdLux.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdMedia.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdMtlx.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdPhysics.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdProc.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdProcImaging.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdRender.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdRi.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdRiPxrImaging.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdSemantics.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdShade.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdSkel.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdSkelImaging.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdUI.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdUtils.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdVol.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdVolImaging.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_vt.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_work.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbb.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbbbind.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbbbind_debug.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbbmalloc.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbbmalloc_debug.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbbmalloc_proxy.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbbmalloc_proxy_debug.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbb_debug.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbb_preview.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbb_preview_debug.dll");

        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_ar.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_arch.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_cameraUtil.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_garch.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_geomUtil.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_gf.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_glf.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hd.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hdar.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hdGp.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hdMtlx.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hdsi.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hdSt.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hdx.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hf.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hgi.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hgiGL.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hgiInterop.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_hio.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_js.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_kind.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_ndr.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_pcp.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_pegtl.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_plug.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_pxOsd.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_sdf.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_sdr.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_tf.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_trace.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_ts.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usd.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdAppUtils.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdBakeMtlx.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdGeom.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdHydra.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdImaging.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdImagingGL.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdLux.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdMedia.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdMtlx.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdPhysics.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdProc.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdProcImaging.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdRender.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdRi.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdRiPxrImaging.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdSemantics.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdShade.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdSkel.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdSkelImaging.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdUI.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdUtils.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdVol.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_usdVolImaging.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_vt.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\usd_work.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbb.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbbbind.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbbbind_debug.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbbmalloc.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbbmalloc_debug.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbbmalloc_proxy.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbbmalloc_proxy_debug.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbb_debug.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbb_preview.pdb");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Debug\tbb_preview_debug.pdb");
      }
      else if (target.Optimization == Optimization.Release)
      {
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\MaterialXCore.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\MaterialXFormat.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\MaterialXGenGlsl.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\MaterialXGenMdl.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\MaterialXGenMsl.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\MaterialXGenOsl.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\MaterialXGenShader.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\MaterialXRender.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\MaterialXRenderGlsl.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\MaterialXRenderHw.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\MaterialXRenderOsl.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\osdCPU.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\osdGPU.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\tbb.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\tbbbind.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\tbbbind_debug.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\tbbmalloc.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\tbbmalloc_debug.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\tbbmalloc_proxy.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\tbbmalloc_proxy_debug.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\tbbproxy.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\tbbproxy_debug.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\tbb_debug.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\tbb_preview.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\tbb_preview_debug.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_ar.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_arch.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_cameraUtil.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_garch.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_geomUtil.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_gf.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_glf.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hd.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hdar.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hdGp.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hdMtlx.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hdsi.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hdSt.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hdx.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hf.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hgi.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hgiGL.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hgiInterop.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hio.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_js.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_kind.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_ndr.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_pcp.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_pegtl.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_plug.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_pxOsd.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_sdf.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_sdr.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_tf.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_trace.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_ts.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usd.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdAppUtils.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdBakeMtlx.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdGeom.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdHydra.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdImaging.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdImagingGL.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdLux.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdMedia.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdMtlx.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdPhysics.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdProc.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdProcImaging.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdRender.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdRi.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdRiPxrImaging.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdSemantics.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdShade.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdSkel.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdSkelImaging.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdUI.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdUtils.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdVol.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdVolImaging.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_vt.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_work.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\zlib.lib");
        conf.LibraryFiles.Add(@"[project.SourceRootPath]\Lib\Release\zlibstatic.lib");

        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_ar.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_arch.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_cameraUtil.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_garch.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_geomUtil.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_gf.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_glf.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hd.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hdar.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hdGp.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hdMtlx.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hdsi.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hdSt.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hdx.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hf.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hgi.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hgiGL.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hgiInterop.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_hio.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_js.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_kind.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_ndr.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_pcp.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_pegtl.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_plug.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_pxOsd.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_sdf.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_sdr.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_tf.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_trace.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_ts.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usd.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdAppUtils.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdBakeMtlx.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdGeom.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdHydra.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdImaging.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdImagingGL.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdLux.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdMedia.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdMtlx.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdPhysics.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdProc.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdProcImaging.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdRender.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdRi.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdRiPxrImaging.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdSemantics.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdShade.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdSkel.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdSkelImaging.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdUI.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdUtils.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdVol.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_usdVolImaging.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_vt.dll");
        conf.TargetCopyFiles.Add(@"[project.SourceRootPath]\Lib\Release\usd_work.dll");
      }
    }
  }

  [Sharpmake.Generate]
  class MaterialGraphEditorProject : AthenaToolQtProject
  {
    public MaterialGraphEditorProject()
    {
      Name = "MaterialGraphEditor";
      SourceRootPath = @"[project.SharpmakeCsPath]\Code\Core\Tools\MaterialGraphEditor";
    }

    [Configure]
    public override void ConfigureAll(Configuration conf, Target target)
    {
      base.ConfigureAll(conf, target);
      conf.Output = Configuration.OutputType.Exe;
      conf.AddPublicDependency<FoundationProject>(target);
    }
  }

  [Sharpmake.Generate]
  public class AthenaSln : Sharpmake.Solution
  {
    public AthenaSln()
    {
      Name = "Athena";

      AddTargets(
        new Target(
          Platform.win64,
          DevEnv.vs2022,
          Optimization.Debug | Optimization.Release
        )
      );
    }

    [Configure]
    public void ConfigureAll(Configuration conf, Target target)
    {
      conf.SolutionFileName = "[solution.Name]";
      conf.SolutionPath = @"[solution.SharpmakeCsPath]\VS";
      conf.AddProject<EngineProject>(target);
      conf.AddProject<FoundationProject>(target);
      conf.AddProject<AssetBuilderProject>(target);
      conf.AddProject<UsdBuilderProject>(target);
      conf.AddProject<MaterialGraphEditorProject>(target);
    }
  }

  public static class Main
  {
    [Sharpmake.Main]
    public static void SharpmakeMain(Sharpmake.Arguments arguments)
    {
      arguments.Generate<AthenaSln>();
    }
  }
}
