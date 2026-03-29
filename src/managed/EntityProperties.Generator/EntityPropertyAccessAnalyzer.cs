using System.Collections.Immutable;
using System.Linq;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Diagnostics;
using Microsoft.CodeAnalysis.Operations;

namespace XServer.Managed.EntityProperties.Generator
{
    [DiagnosticAnalyzer(LanguageNames.CSharp)]
    public sealed class EntityPropertyAccessAnalyzer : DiagnosticAnalyzer
    {
        public const string DiagnosticId = "XEPG008";

        private const string EntityPropertyAttributeMetadataName =
            "XServer.Managed.Framework.Entities.EntityPropertyAttribute";

        private static readonly DiagnosticDescriptor DirectEntityPropertyAccessRule =
            new(
                id: DiagnosticId,
                title: "EntityProperty field must be accessed through generated interface",
                messageFormat:
                "Field '{0}' is marked with EntityProperty and cannot be accessed directly. Use generated interface '{1}' member '{2}' instead.",
                category: "EntityPropertyGenerator",
                defaultSeverity: DiagnosticSeverity.Error,
                isEnabledByDefault: true);

        public override ImmutableArray<DiagnosticDescriptor> SupportedDiagnostics =>
            [DirectEntityPropertyAccessRule];

        public override void Initialize(AnalysisContext context)
        {
            context.ConfigureGeneratedCodeAnalysis(GeneratedCodeAnalysisFlags.None);
            context.EnableConcurrentExecution();

            context.RegisterCompilationStartAction(
                static compilationContext =>
                {
                    INamedTypeSymbol? entityPropertyAttributeSymbol =
                        compilationContext.Compilation.GetTypeByMetadataName(EntityPropertyAttributeMetadataName);
                    if (entityPropertyAttributeSymbol is null)
                    {
                        return;
                    }

                    compilationContext.RegisterOperationAction(
                        operationContext => AnalyzeFieldReference(operationContext, entityPropertyAttributeSymbol),
                        OperationKind.FieldReference);
                });
        }

        private static void AnalyzeFieldReference(
            OperationAnalysisContext context,
            INamedTypeSymbol entityPropertyAttributeSymbol)
        {
            if (context.Operation is not IFieldReferenceOperation fieldReference)
            {
                return;
            }

            IFieldSymbol fieldSymbol = fieldReference.Field;
            if (!fieldSymbol.GetAttributes().Any(
                    attribute =>
                        SymbolEqualityComparer.Default.Equals(
                            attribute.AttributeClass,
                            entityPropertyAttributeSymbol)))
            {
                return;
            }

            if (HasNameOfAncestor(fieldReference))
            {
                return;
            }

            string generatedPropertyName = GetGeneratedPropertyName(fieldSymbol);
            string interfaceName = "I" + fieldSymbol.ContainingType.Name + "Properties";

            context.ReportDiagnostic(
                Diagnostic.Create(
                    DirectEntityPropertyAccessRule,
                    fieldReference.Syntax.GetLocation(),
                    fieldSymbol.Name,
                    interfaceName,
                    generatedPropertyName));
        }

        private static bool HasNameOfAncestor(IOperation operation)
        {
            for (IOperation? current = operation.Parent; current is not null; current = current.Parent)
            {
                if (current.Kind == OperationKind.NameOf)
                {
                    return true;
                }
            }

            return false;
        }

        private static string GetGeneratedPropertyName(IFieldSymbol fieldSymbol)
        {
            return fieldSymbol.Name.StartsWith("__", System.StringComparison.Ordinal) &&
                   fieldSymbol.Name.Length > 2
                ? fieldSymbol.Name.Substring(2)
                : fieldSymbol.Name;
        }
    }
}
