using Sharpmake;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace Athena
{
  abstract class AthenaProject : Project
  {
    public Strings BuiltShaderHeaders;
    public AthenaProject()
    {
      BuiltShaderHeaders = new Strings();
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
      conf.Options.Add(Options.Vc.General.TreatWarningsAsErrors.Enable);
      conf.Options.Add(Options.Vc.Linker.CreateHotPatchableImage.Enable);
      conf.Options.Add(Options.Vc.Linker.EnableCOMDATFolding.DoNotRemoveRedundantCOMDATs);
      conf.Options.Add(Options.Vc.Linker.GenerateDebugInformation.Enable);
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
    }
  }

  [Sharpmake.Generate]
  class CustomBuildShaderFile : Project.Configuration.CustomFileBuildStep
  {
    public CustomBuildShaderFile(AthenaProject project, string src, string dst)
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

    public static void ClaimShaderFiles(AthenaProject project)
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

    public override void ConfigureAll(Configuration conf, Target target)
    {
      base.ConfigureAll(conf, target);
      conf.Defines.Add("FOUNDATION_EXPORT");
      conf.IncludePaths.Add(@"[project.SourceRootPath\..\..\..\");
      conf.Output = Configuration.OutputType.Dll;
    }
  }

  [Sharpmake.Generate]
  class EngineProject : AthenaProject
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
      conf.VcxprojUserFile = new Configuration.VcxprojUserFileSettings();
      conf.VcxprojUserFile.LocalDebuggerWorkingDirectory = @"[project.SharpmakeCsPath]";
      conf.Options.Add(Options.Vc.Linker.SubSystem.Windows);
    }
  }

  [Sharpmake.Generate]
  class AssetBuilderProject : AthenaProject
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
      conf.VcxprojUserFile = new Configuration.VcxprojUserFileSettings();
      conf.VcxprojUserFile.LocalDebuggerWorkingDirectory = @"[project.SharpmakeCsPath]";
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
