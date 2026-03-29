using System.Collections.Immutable;
using System.IO;
using System.Linq;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Diagnostics;
using XServer.Managed.EntityProperties.Generator;
using XServer.Managed.Framework.Entities;

namespace XServer.Managed.Framework.Tests
{
    public class EntityPropertyAnalyzerTests
    {
        [Fact]
        public async Task EntityPropertyAccessAnalyzer_ReportsDirectFieldReadAndWrite()
        {
            Compilation compilation = CreateCompilation(
                """
                using XServer.Managed.Framework.Entities;

                namespace AnalyzerSamples
                {
                    public partial class SampleEntity : ServerEntity
                    {
                        [EntityProperty(EntityPropertyFlags.AllClients)]
                        protected int __Score;

                        public void SetScoreDirect(int value)
                        {
                            __Score = value;
                        }

                        public int GetScoreDirect()
                        {
                            return __Score;
                        }
                    }
                }
                """);

            Compilation generatedCompilation = RunSourceGenerator(compilation, out ImmutableArray<Diagnostic> generatorDiagnostics);

            Assert.DoesNotContain(generatorDiagnostics, diagnostic => diagnostic.Severity == DiagnosticSeverity.Error);
            Assert.DoesNotContain(generatedCompilation.GetDiagnostics(), diagnostic => diagnostic.Severity == DiagnosticSeverity.Error);

            ImmutableArray<Diagnostic> analyzerDiagnostics =
                await GetAnalyzerDiagnosticsAsync(generatedCompilation);
            ImmutableArray<Diagnostic> directAccessDiagnostics = analyzerDiagnostics
                .Where(static diagnostic => diagnostic.Id == EntityPropertyAccessAnalyzer.DiagnosticId)
                .OrderBy(static diagnostic => diagnostic.Location.SourceSpan.Start)
                .ToImmutableArray();

            Assert.Equal(2, directAccessDiagnostics.Length);
            Assert.All(
                directAccessDiagnostics,
                diagnostic =>
                {
                    Assert.Contains("__Score", diagnostic.GetMessage(), System.StringComparison.Ordinal);
                    Assert.Contains("ISampleEntityProperties", diagnostic.GetMessage(), System.StringComparison.Ordinal);
                    Assert.Contains("Score", diagnostic.GetMessage(), System.StringComparison.Ordinal);
                });
        }

        [Fact]
        public async Task EntityPropertyAccessAnalyzer_AllowsGeneratedInterfaceAccess()
        {
            Compilation compilation = CreateCompilation(
                """
                using XServer.Managed.Framework.Entities;

                namespace AnalyzerSamples
                {
                    public partial class SampleEntity : ServerEntity
                    {
                        [EntityProperty(EntityPropertyFlags.AllClients)]
                        protected int __Score;

                        public void SetScoreViaInterface(int value)
                        {
                            ((ISampleEntityProperties)this).Score = value;
                        }

                        public int GetScoreViaInterface()
                        {
                            return ((ISampleEntityProperties)this).Score;
                        }
                    }
                }
                """);

            Compilation generatedCompilation = RunSourceGenerator(compilation, out ImmutableArray<Diagnostic> generatorDiagnostics);

            Assert.DoesNotContain(generatorDiagnostics, diagnostic => diagnostic.Severity == DiagnosticSeverity.Error);
            Assert.DoesNotContain(generatedCompilation.GetDiagnostics(), diagnostic => diagnostic.Severity == DiagnosticSeverity.Error);

            ImmutableArray<Diagnostic> analyzerDiagnostics =
                await GetAnalyzerDiagnosticsAsync(generatedCompilation);

            Assert.DoesNotContain(
                analyzerDiagnostics,
                diagnostic => diagnostic.Id == EntityPropertyAccessAnalyzer.DiagnosticId);
        }

        [Fact]
        public async Task EntityPropertyAccessAnalyzer_AllowsNameOfMarkedField()
        {
            Compilation compilation = CreateCompilation(
                """
                using XServer.Managed.Framework.Entities;

                namespace AnalyzerSamples
                {
                    public partial class SampleEntity : ServerEntity
                    {
                        [EntityProperty(EntityPropertyFlags.AllClients)]
                        protected int __Score;

                        public string GetScoreFieldName()
                        {
                            return nameof(__Score);
                        }
                    }
                }
                """);

            Compilation generatedCompilation = RunSourceGenerator(compilation, out ImmutableArray<Diagnostic> generatorDiagnostics);

            Assert.DoesNotContain(generatorDiagnostics, diagnostic => diagnostic.Severity == DiagnosticSeverity.Error);
            Assert.DoesNotContain(generatedCompilation.GetDiagnostics(), diagnostic => diagnostic.Severity == DiagnosticSeverity.Error);

            ImmutableArray<Diagnostic> analyzerDiagnostics =
                await GetAnalyzerDiagnosticsAsync(generatedCompilation);

            Assert.DoesNotContain(
                analyzerDiagnostics,
                diagnostic => diagnostic.Id == EntityPropertyAccessAnalyzer.DiagnosticId);
        }

        private static Compilation CreateCompilation(string source)
        {
            SyntaxTree syntaxTree =
                CSharpSyntaxTree.ParseText(
                    source,
                    new CSharpParseOptions(LanguageVersion.Latest));

            return CSharpCompilation.Create(
                assemblyName: "EntityPropertyAnalyzerSample",
                syntaxTrees: [syntaxTree],
                references: CreateMetadataReferences(),
                options: new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary));
        }

        private static Compilation RunSourceGenerator(
            Compilation compilation,
            out ImmutableArray<Diagnostic> generatorDiagnostics)
        {
            GeneratorDriver driver =
                CSharpGeneratorDriver.Create(
                    [new EntityPropertySourceGenerator().AsSourceGenerator()]);

            driver.RunGeneratorsAndUpdateCompilation(
                compilation,
                out Compilation updatedCompilation,
                out generatorDiagnostics);

            return updatedCompilation;
        }

        private static async Task<ImmutableArray<Diagnostic>> GetAnalyzerDiagnosticsAsync(Compilation compilation)
        {
            CompilationWithAnalyzers compilationWithAnalyzers =
                compilation.WithAnalyzers(
                    [new EntityPropertyAccessAnalyzer()]);

            ImmutableArray<Diagnostic> diagnostics =
                await compilationWithAnalyzers.GetAnalyzerDiagnosticsAsync();

            return diagnostics
                .OrderBy(static diagnostic => diagnostic.Location.SourceSpan.Start)
                .ToImmutableArray();
        }

        private static ImmutableArray<MetadataReference> CreateMetadataReferences()
        {
            string trustedPlatformAssemblies =
                (string)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")!;

            return trustedPlatformAssemblies
                .Split(Path.PathSeparator)
                .Append(typeof(ServerEntity).Assembly.Location)
                .Distinct(System.StringComparer.OrdinalIgnoreCase)
                .Select(static path => (MetadataReference)MetadataReference.CreateFromFile(path))
                .ToImmutableArray();
        }
    }
}
