using System.Collections.Generic;
using System.Collections.Immutable;
using System.Linq;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace XServer.Managed.EntityProperties.Generator
{
    [Generator]
    public sealed class EntityPropertySourceGenerator : IIncrementalGenerator
    {
        private const string EntityPropertyAttributeMetadataName =
            "XServer.Managed.Framework.Entities.EntityPropertyAttribute";
        private const string ServerEntityMetadataName = "XServer.Managed.Framework.Entities.ServerEntity";

        private static readonly SymbolDisplayFormat FullyQualifiedTypeFormat =
            SymbolDisplayFormat.FullyQualifiedFormat.WithMiscellaneousOptions(
                SymbolDisplayMiscellaneousOptions.IncludeNullableReferenceTypeModifier);

        private static readonly DiagnosticDescriptor ContainingTypeMustDeriveFromServerEntityRule =
            new(
                id: "XEPG001",
                title: "EntityProperty field must belong to a ServerEntity type",
                messageFormat:
                "Field '{0}' is marked with EntityProperty but its containing type does not derive from ServerEntity.",
                category: "EntityPropertyGenerator",
                defaultSeverity: DiagnosticSeverity.Error,
                isEnabledByDefault: true);

        private static readonly DiagnosticDescriptor ContainingTypeMustBePartialRule =
            new(
                id: "XEPG002",
                title: "EntityProperty field requires a partial type",
                messageFormat:
                "Type '{0}' declares EntityProperty field '{1}' and must be declared partial so generated accessors can be emitted.",
                category: "EntityPropertyGenerator",
                defaultSeverity: DiagnosticSeverity.Error,
                isEnabledByDefault: true);

        private static readonly DiagnosticDescriptor NestedTypeNotSupportedRule =
            new(
                id: "XEPG003",
                title: "Nested entity types are not supported",
                messageFormat:
                "Type '{0}' declares EntityProperty field '{1}' but nested entity types are not supported by the current generator implementation.",
                category: "EntityPropertyGenerator",
                defaultSeverity: DiagnosticSeverity.Error,
                isEnabledByDefault: true);

        private static readonly DiagnosticDescriptor FieldMustBeProtectedRule =
            new(
                id: "XEPG004",
                title: "EntityProperty field must be protected",
                messageFormat:
                "Field '{0}' is marked with EntityProperty and must be declared protected.",
                category: "EntityPropertyGenerator",
                defaultSeverity: DiagnosticSeverity.Error,
                isEnabledByDefault: true);

        private static readonly DiagnosticDescriptor FieldMustUseDoubleUnderscorePrefixRule =
            new(
                id: "XEPG005",
                title: "EntityProperty field must use the required prefix",
                messageFormat:
                "Field '{0}' is marked with EntityProperty and must start with '__' followed by the generated property name.",
                category: "EntityPropertyGenerator",
                defaultSeverity: DiagnosticSeverity.Error,
                isEnabledByDefault: true);

        private static readonly DiagnosticDescriptor FieldMustBeWritableInstanceFieldRule =
            new(
                id: "XEPG006",
                title: "EntityProperty field must be writable",
                messageFormat:
                "Field '{0}' is marked with EntityProperty and must be a writable instance field so getter and setter accessors can be generated.",
                category: "EntityPropertyGenerator",
                defaultSeverity: DiagnosticSeverity.Error,
                isEnabledByDefault: true);

        private static readonly DiagnosticDescriptor GeneratedPropertyNameCollisionRule =
            new(
                id: "XEPG007",
                title: "Generated property name collides with an existing member",
                messageFormat:
                "Field '{0}' would generate property '{1}', but that member name already exists on type '{2}' or one of its base types.",
                category: "EntityPropertyGenerator",
                defaultSeverity: DiagnosticSeverity.Error,
                isEnabledByDefault: true);

        public void Initialize(IncrementalGeneratorInitializationContext context)
        {
            IncrementalValuesProvider<FieldCandidate> fieldCandidates =
                context.SyntaxProvider
                    .CreateSyntaxProvider(
                        static (node, _) =>
                            node is VariableDeclaratorSyntax
                            {
                                Parent: VariableDeclarationSyntax
                                {
                                    Parent: FieldDeclarationSyntax { AttributeLists.Count: > 0 },
                                },
                            },
                        static (syntaxContext, _) => GetFieldCandidate(syntaxContext))
                    .Where(static candidate => candidate is not null)
                    .Select(static (candidate, _) => candidate!);

            IncrementalValueProvider<(Compilation Left, ImmutableArray<FieldCandidate> Right)> source =
                context.CompilationProvider.Combine(fieldCandidates.Collect());

            context.RegisterSourceOutput(
                source,
                static (productionContext, input) => Execute(productionContext, input.Left, input.Right));
        }

        private static FieldCandidate? GetFieldCandidate(GeneratorSyntaxContext syntaxContext)
        {
            if (syntaxContext.Node is not VariableDeclaratorSyntax variableDeclarator)
            {
                return null;
            }

            if (syntaxContext.SemanticModel.GetDeclaredSymbol(variableDeclarator) is not IFieldSymbol fieldSymbol)
            {
                return null;
            }

            bool hasEntityPropertyAttribute = fieldSymbol.GetAttributes().Any(
                attribute => attribute.AttributeClass?.ToDisplayString() == EntityPropertyAttributeMetadataName);

            return hasEntityPropertyAttribute ? new FieldCandidate(fieldSymbol) : null;
        }

        private static void Execute(
            SourceProductionContext context,
            Compilation compilation,
            ImmutableArray<FieldCandidate> fieldCandidates)
        {
            INamedTypeSymbol? serverEntitySymbol =
                compilation.GetTypeByMetadataName(ServerEntityMetadataName);
            if (serverEntitySymbol is null)
            {
                return;
            }

            var validatedFieldsByType =
                new Dictionary<INamedTypeSymbol, List<ValidatedField>>(SymbolEqualityComparer.Default);

            foreach (FieldCandidate fieldCandidate in fieldCandidates)
            {
                ValidatedField? validatedField = ValidateField(
                    fieldCandidate.FieldSymbol,
                    serverEntitySymbol,
                    context);
                if (validatedField is null)
                {
                    continue;
                }

                if (!validatedFieldsByType.TryGetValue(
                        validatedField.ContainingType,
                        out List<ValidatedField>? validatedFields))
                {
                    validatedFields = new List<ValidatedField>();
                    validatedFieldsByType.Add(validatedField.ContainingType, validatedFields);
                }

                validatedFields.Add(validatedField);
            }

            foreach (INamedTypeSymbol containingType in validatedFieldsByType.Keys.OrderBy(
                         static symbol => symbol.ToDisplayString(),
                         System.StringComparer.Ordinal))
            {
                GenerateAccessorsForType(context, containingType, validatedFieldsByType);
            }
        }

        private static ValidatedField? ValidateField(
            IFieldSymbol fieldSymbol,
            INamedTypeSymbol serverEntitySymbol,
            SourceProductionContext context)
        {
            INamedTypeSymbol containingType = fieldSymbol.ContainingType;

            if (!DerivesFromOrEquals(containingType, serverEntitySymbol))
            {
                context.ReportDiagnostic(
                    Diagnostic.Create(
                        ContainingTypeMustDeriveFromServerEntityRule,
                        fieldSymbol.Locations.FirstOrDefault(),
                        fieldSymbol.Name));
                return null;
            }

            if (containingType.ContainingType is not null)
            {
                context.ReportDiagnostic(
                    Diagnostic.Create(
                        NestedTypeNotSupportedRule,
                        fieldSymbol.Locations.FirstOrDefault(),
                        containingType.ToDisplayString(),
                        fieldSymbol.Name));
                return null;
            }

            if (!IsPartialType(containingType))
            {
                context.ReportDiagnostic(
                    Diagnostic.Create(
                        ContainingTypeMustBePartialRule,
                        fieldSymbol.Locations.FirstOrDefault(),
                        containingType.ToDisplayString(),
                        fieldSymbol.Name));
                return null;
            }

            if (fieldSymbol.DeclaredAccessibility != Accessibility.Protected)
            {
                context.ReportDiagnostic(
                    Diagnostic.Create(
                        FieldMustBeProtectedRule,
                        fieldSymbol.Locations.FirstOrDefault(),
                        fieldSymbol.Name));
                return null;
            }

            if (!fieldSymbol.Name.StartsWith("__", System.StringComparison.Ordinal) || fieldSymbol.Name.Length <= 2)
            {
                context.ReportDiagnostic(
                    Diagnostic.Create(
                        FieldMustUseDoubleUnderscorePrefixRule,
                        fieldSymbol.Locations.FirstOrDefault(),
                        fieldSymbol.Name));
                return null;
            }

            if (fieldSymbol.IsStatic || fieldSymbol.IsConst || fieldSymbol.IsReadOnly)
            {
                context.ReportDiagnostic(
                    Diagnostic.Create(
                        FieldMustBeWritableInstanceFieldRule,
                        fieldSymbol.Locations.FirstOrDefault(),
                        fieldSymbol.Name));
                return null;
            }

            string generatedPropertyName = fieldSymbol.Name.Substring(2);
            if (!SyntaxFacts.IsValidIdentifier(generatedPropertyName))
            {
                context.ReportDiagnostic(
                    Diagnostic.Create(
                        FieldMustUseDoubleUnderscorePrefixRule,
                        fieldSymbol.Locations.FirstOrDefault(),
                        fieldSymbol.Name));
                return null;
            }

            if (HasGeneratedPropertyNameCollision(containingType, generatedPropertyName))
            {
                context.ReportDiagnostic(
                    Diagnostic.Create(
                        GeneratedPropertyNameCollisionRule,
                        fieldSymbol.Locations.FirstOrDefault(),
                        fieldSymbol.Name,
                        generatedPropertyName,
                        containingType.ToDisplayString()));
                return null;
            }

            return new ValidatedField(fieldSymbol, containingType, generatedPropertyName);
        }

        private static void GenerateAccessorsForType(
            SourceProductionContext context,
            INamedTypeSymbol containingType,
            Dictionary<INamedTypeSymbol, List<ValidatedField>> validatedFieldsByType)
        {
            List<ValidatedField> localFields = validatedFieldsByType[containingType]
                .OrderBy(static field => field.FieldSymbol.Locations.First().SourceSpan.Start)
                .ToList();

            var localPropertyNames = new HashSet<string>(System.StringComparer.Ordinal);
            foreach (ValidatedField localField in localFields)
            {
                if (!localPropertyNames.Add(localField.GeneratedPropertyName))
                {
                    context.ReportDiagnostic(
                        Diagnostic.Create(
                            GeneratedPropertyNameCollisionRule,
                            localField.FieldSymbol.Locations.FirstOrDefault(),
                            localField.FieldSymbol.Name,
                            localField.GeneratedPropertyName,
                            containingType.ToDisplayString()));
                    return;
                }
            }

            string interfaceName = GetInterfaceName(containingType);
            string? baseInterfaceName =
                containingType.BaseType is not null && validatedFieldsByType.ContainsKey(containingType.BaseType)
                    ? GetInterfaceName(containingType.BaseType)
                    : null;

            var sourceBuilder = new StringBuilder();
            sourceBuilder.AppendLine("// <auto-generated />");
            sourceBuilder.AppendLine("#nullable enable");

            if (!containingType.ContainingNamespace.IsGlobalNamespace)
            {
                sourceBuilder.Append("namespace ");
                sourceBuilder.Append(containingType.ContainingNamespace.ToDisplayString());
                sourceBuilder.AppendLine();
                sourceBuilder.AppendLine("{");
            }

            string indentation = containingType.ContainingNamespace.IsGlobalNamespace ? string.Empty : "    ";
            string interfaceAccessibility = GetAccessibilityKeyword(containingType.DeclaredAccessibility);

            sourceBuilder.Append(indentation);
            sourceBuilder.Append(interfaceAccessibility);
            sourceBuilder.Append(" interface ");
            sourceBuilder.Append(interfaceName);
            if (baseInterfaceName is not null)
            {
                sourceBuilder.Append(" : ");
                sourceBuilder.Append(baseInterfaceName);
            }

            sourceBuilder.AppendLine();
            sourceBuilder.Append(indentation);
            sourceBuilder.AppendLine("{");

            foreach (ValidatedField localField in localFields)
            {
                sourceBuilder.Append(indentation);
                sourceBuilder.Append("    ");
                sourceBuilder.Append(localField.FieldSymbol.Type.ToDisplayString(FullyQualifiedTypeFormat));
                sourceBuilder.Append(' ');
                sourceBuilder.Append(localField.GeneratedPropertyName);
                sourceBuilder.AppendLine(" { get; set; }");
            }

            sourceBuilder.Append(indentation);
            sourceBuilder.AppendLine("}");
            sourceBuilder.AppendLine();

            sourceBuilder.Append(indentation);
            sourceBuilder.Append(GetTypeDeclarationHeader(containingType));
            sourceBuilder.Append(" : ");
            sourceBuilder.Append(interfaceName);
            sourceBuilder.AppendLine();
            sourceBuilder.Append(indentation);
            sourceBuilder.AppendLine("{");

            foreach (ValidatedField localField in localFields)
            {
                sourceBuilder.Append(indentation);
                sourceBuilder.Append("    public ");
                sourceBuilder.Append(localField.FieldSymbol.Type.ToDisplayString(FullyQualifiedTypeFormat));
                sourceBuilder.Append(' ');
                sourceBuilder.Append(localField.GeneratedPropertyName);
                sourceBuilder.AppendLine();
                sourceBuilder.Append(indentation);
                sourceBuilder.AppendLine("    {");
                sourceBuilder.Append(indentation);
                sourceBuilder.Append("        get => ");
                sourceBuilder.Append(localField.FieldSymbol.Name);
                sourceBuilder.AppendLine(";");
                sourceBuilder.Append(indentation);
                sourceBuilder.Append("        set => ");
                sourceBuilder.Append(localField.FieldSymbol.Name);
                sourceBuilder.AppendLine(" = value;");
                sourceBuilder.Append(indentation);
                sourceBuilder.AppendLine("    }");
                sourceBuilder.AppendLine();
            }

            sourceBuilder.Append(indentation);
            sourceBuilder.AppendLine("}");

            if (!containingType.ContainingNamespace.IsGlobalNamespace)
            {
                sourceBuilder.AppendLine("}");
            }

            context.AddSource(GetHintName(containingType), sourceBuilder.ToString());
        }

        private static bool DerivesFromOrEquals(
            INamedTypeSymbol candidateType,
            INamedTypeSymbol targetType)
        {
            for (INamedTypeSymbol? currentType = candidateType;
                 currentType is not null;
                 currentType = currentType.BaseType)
            {
                if (SymbolEqualityComparer.Default.Equals(currentType, targetType))
                {
                    return true;
                }
            }

            return false;
        }

        private static bool IsPartialType(INamedTypeSymbol typeSymbol)
        {
            foreach (SyntaxReference syntaxReference in typeSymbol.DeclaringSyntaxReferences)
            {
                if (syntaxReference.GetSyntax() is TypeDeclarationSyntax typeDeclarationSyntax &&
                    typeDeclarationSyntax.Modifiers.Any(SyntaxKind.PartialKeyword))
                {
                    return true;
                }
            }

            return false;
        }

        private static bool HasGeneratedPropertyNameCollision(
            INamedTypeSymbol containingType,
            string propertyName)
        {
            for (INamedTypeSymbol? currentType = containingType;
                 currentType is not null;
                 currentType = currentType.BaseType)
            {
                if (currentType.GetMembers(propertyName).Length > 0)
                {
                    return true;
                }
            }

            return false;
        }

        private static string GetAccessibilityKeyword(Accessibility accessibility)
        {
            return accessibility switch
            {
                Accessibility.Public => "public",
                Accessibility.Internal => "internal",
                Accessibility.Private => "private",
                Accessibility.Protected => "protected",
                Accessibility.ProtectedAndInternal => "private protected",
                Accessibility.ProtectedOrInternal => "protected internal",
                _ => "internal",
            };
        }

        private static string GetTypeDeclarationHeader(INamedTypeSymbol typeSymbol)
        {
            var builder = new StringBuilder();
            builder.Append(GetAccessibilityKeyword(typeSymbol.DeclaredAccessibility));
            builder.Append(' ');

            if (typeSymbol.IsAbstract && !typeSymbol.IsSealed)
            {
                builder.Append("abstract ");
            }
            else if (typeSymbol.IsSealed)
            {
                builder.Append("sealed ");
            }

            builder.Append("partial class ");
            builder.Append(typeSymbol.Name);
            return builder.ToString();
        }

        private static string GetInterfaceName(INamedTypeSymbol typeSymbol)
        {
            return "I" + typeSymbol.Name + "Properties";
        }

        private static string GetHintName(INamedTypeSymbol typeSymbol)
        {
            return typeSymbol.ToDisplayString().Replace('.', '_') + ".EntityProperties.g.cs";
        }

        private sealed class FieldCandidate
        {
            public FieldCandidate(IFieldSymbol fieldSymbol)
            {
                FieldSymbol = fieldSymbol;
            }

            public IFieldSymbol FieldSymbol { get; }
        }

        private sealed class ValidatedField
        {
            public ValidatedField(
                IFieldSymbol fieldSymbol,
                INamedTypeSymbol containingType,
                string generatedPropertyName)
            {
                FieldSymbol = fieldSymbol;
                ContainingType = containingType;
                GeneratedPropertyName = generatedPropertyName;
            }

            public IFieldSymbol FieldSymbol { get; }

            public INamedTypeSymbol ContainingType { get; }

            public string GeneratedPropertyName { get; }
        }
    }
}
