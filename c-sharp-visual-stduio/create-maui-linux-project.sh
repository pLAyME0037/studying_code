#!/bin/bash
set -e

PROJECT_NAME="${1:?Usage: $0 <ProjectName> [OutputPath]}"
OUTPUT_PATH="${2:-.}"

echo "Creating MAUI project: $PROJECT_NAME"

# Step 1: Create standard MAUI project
dotnet new maui -n "$PROJECT_NAME" -o "$OUTPUT_PATH"

CSPROJ="$OUTPUT_PATH/$PROJECT_NAME.csproj"

if [ ! -f "$CSPROJ" ]; then
    echo "Error: csproj not found at $CSPROJ"
    exit 1
fi

# Step 2: Create Linux platform directory
mkdir -p "$OUTPUT_PATH/Platforms/Linux"

# Step 3: Write Linux/Program.cs (using sed for namespace)
cat > "$OUTPUT_PATH/Platforms/Linux/Program.cs" << 'LINUX_EOF'
using Microsoft.Maui.Platform.Linux;
using Microsoft.Maui.Platform.Linux.Hosting;

namespace __NAMESPACE__;

public class Program
{
    public static void Main(string[] args)
    {
        var app = MauiProgram.CreateMauiAppBuilder()
            .UseLinux()
            .Build();
        LinuxApplication.Run(app, args);
    }
}
LINUX_EOF

sed -i "s/__NAMESPACE__/$PROJECT_NAME/g" "$OUTPUT_PATH/Platforms/Linux/Program.cs"

# Step 4: Rewrite csproj with Linux support (quoted heredoc to prevent expansion)
cat > "$CSPROJ" << 'CSPROJ_EOF'
<Project Sdk="Microsoft.NET.Sdk">

	<PropertyGroup>
		<TargetFrameworks>net10.0-android</TargetFrameworks>
		<TargetFrameworks Condition="$([MSBuild]::IsOSPlatform('linux'))">net10.0</TargetFrameworks>
		<TargetFrameworks Condition="!$([MSBuild]::IsOSPlatform('linux'))">$(TargetFrameworks);net10.0-ios;net10.0-maccatalyst</TargetFrameworks>
		<TargetFrameworks Condition="$([MSBuild]::IsOSPlatform('windows'))">$(TargetFrameworks);net10.0-windows10.0.19041.0</TargetFrameworks>

		<OutputType>Exe</OutputType>
		<RootNamespace>__PROJECT_NAME__</RootNamespace>
		<UseMaui>true</UseMaui>
		<SingleProject>true</SingleProject>
		<ImplicitUsings>enable</ImplicitUsings>
		<Nullable>enable</Nullable>

		<ApplicationTitle>__PROJECT_NAME__</ApplicationTitle>
		<ApplicationId>com.companyname.__APP_ID__</ApplicationId>
		<ApplicationDisplayVersion>1.0</ApplicationDisplayVersion>
		<ApplicationVersion>1</ApplicationVersion>

		<WindowsPackageType>None</WindowsPackageType>

		<SupportedOSPlatformVersion Condition="$([MSBuild]::GetTargetPlatformIdentifier('$(TargetFramework)')) == 'ios'">15.0</SupportedOSPlatformVersion>
		<SupportedOSPlatformVersion Condition="$([MSBuild]::GetTargetPlatformIdentifier('$(TargetFramework)')) == 'maccatalyst'">15.0</SupportedOSPlatformVersion>
		<SupportedOSPlatformVersion Condition="$([MSBuild]::GetTargetPlatformIdentifier('$(TargetFramework)')) == 'android'">21.0</SupportedOSPlatformVersion>
		<SupportedOSPlatformVersion Condition="$([MSBuild]::GetTargetPlatformIdentifier('$(TargetFramework)')) == 'windows'">10.0.17763.0</SupportedOSPlatformVersion>
		<TargetPlatformMinVersion Condition="$([MSBuild]::GetTargetPlatformIdentifier('$(TargetFramework)')) == 'windows'">10.0.17763.0</TargetPlatformMinVersion>
	</PropertyGroup>

    <PropertyGroup Condition="'$(TargetFramework)' == 'net10.0'">
        <RuntimeIdentifiers>linux-x64;linux-arm64</RuntimeIdentifiers>
    </PropertyGroup>

    <!-- OpenMAUI packages (Linux only) -->
    <ItemGroup Condition="'$(TargetFramework)' == 'net10.0'">
        <PackageReference Include="OpenMaui.Hosting" Version="10.0.*" />
        <PackageReference Include="OpenMaui.Controls.Linux" Version="10.0.*" />
        <PackageReference Include="Microsoft.Maui.Controls" Version="10.0.*" />
        <PackageReference Include="Microsoft.Maui.Controls.Compatibility" Version="10.0.*" />
    </ItemGroup>

    <!-- Exclude non-Linux platform files for Linux build -->
    <ItemGroup Condition="'$(TargetFramework)' == 'net10.0'">
        <Compile Remove="Platforms\Android\**" />
        <Compile Remove="Platforms\Windows\**" />
        <Compile Remove="Platforms\iOS\**" />
        <Compile Remove="Platforms\MacCatalyst\**" />
    </ItemGroup>

    <!-- Exclude Linux platform files for non-Linux builds -->
    <ItemGroup Condition="'$(TargetFramework)' != 'net10.0'">
        <Compile Remove="Platforms\Linux\**" />
    </ItemGroup>

    <!-- XAML Files (Linux only) -->
    <ItemGroup Condition="'$(TargetFramework)' == 'net10.0'">
        <MauiXaml Update="**/*.xaml" />
    </ItemGroup>

    <!-- Resources (Linux only) -->
    <ItemGroup Condition="'$(TargetFramework)' == 'net10.0'">
        <MauiFont Include="Resources\Fonts\*" />
        <MauiImage Include="Resources\Images\*" Exclude="Resources\Images\*.md;Resources\Images\.gitkeep" />
    </ItemGroup>

    <ItemGroup>
        <MauiIcon Include="Resources\AppIcon\appicon.svg" ForegroundFile="Resources\AppIcon\appiconfg.svg" Color="#512BD4" />
        <MauiSplashScreen Include="Resources\Splash\splash.svg" Color="#512BD4" BaseSize="128,128" />
        <MauiImage Include="Resources\Images\*" />
        <MauiImage Update="Resources\Images\dotnet_bot.png" Resize="True" BaseSize="300,185" />
        <MauiFont Include="Resources\Fonts\*" />
        <MauiAsset Include="Resources\Raw\**" LogicalName="%(RecursiveDir)%(Filename)%(Extension)" />
    </ItemGroup>

    <ItemGroup>
        <PackageReference Include="CommunityToolkit.Mvvm" Version="8.4.2" />
        <PackageReference Include="Microsoft.Extensions.Logging.Debug" Version="10.0.0" />
    </ItemGroup>

</Project>
CSPROJ_EOF

# Replace placeholders with actual values
APP_ID=$(echo "$PROJECT_NAME" | tr '[:upper:]' '[:lower:]')
sed -i "s/__PROJECT_NAME__/$PROJECT_NAME/g" "$CSPROJ"
sed -i "s/__APP_ID__/$APP_ID/g" "$CSPROJ"

# Step 5: Write MauiProgram.cs with both methods (builder + app)
cat > "$OUTPUT_PATH/MauiProgram.cs" << 'MAUI_EOF'
using Microsoft.Extensions.Logging;

namespace __NAMESPACE__;

public static class MauiProgram
{
    public static MauiAppBuilder CreateMauiAppBuilder()
    {
        var builder = MauiApp.CreateBuilder();
        builder
            .UseMauiApp<App>()
            .ConfigureFonts(fonts =>
            {
                fonts.AddFont("OpenSans-Regular.ttf", "OpenSansRegular");
                fonts.AddFont("OpenSans-Semibold.ttf", "OpenSansSemibold");
            });

#if DEBUG
    builder.Logging.AddDebug();
#endif

        return builder;
    }

    public static MauiApp CreateMauiApp()
    {
        return CreateMauiAppBuilder().Build();
    }
}
MAUI_EOF

sed -i "s/__NAMESPACE__/$PROJECT_NAME/g" "$OUTPUT_PATH/MauiProgram.cs"

# Step 6: Restore packages
echo "Restoring packages..."
cd "$OUTPUT_PATH"
dotnet restore

echo ""
echo "Done! Project created at: $OUTPUT_PATH"
echo ""
echo "Build commands:"
echo "  Linux:      dotnet build -f net10.0"
echo "  Android:    dotnet build -f net10.0-android"
echo "  iOS:        dotnet build -f net10.0-ios"
echo "  Mac:        dotnet build -f net10.0-maccatalyst"
echo "  Windows:    dotnet build -f net10.0-windows10.0.19041.0"
