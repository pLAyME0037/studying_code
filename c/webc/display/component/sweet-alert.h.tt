<!-- 1. Load the Library -->
<script src="https://cdn.jsdelivr.net/npm/sweetalert2@11"></script>

<!-- 2. Logic -->
<script>
    document.addEventListener('livewire:initialized', () => {
        
        // Helper: Check if Dark Mode is active
        const isDarkMode = () => {
            return document.documentElement.classList.contains('dark') 
            || (window.matchMedia 
            && window.matchMedia('(prefers-color-scheme: dark)').matches);
        };

        // Helper: Get Base Config with Dynamic Colors
        const getSwalConfig = () => {
            const dark = isDarkMode();
            return {
                background: dark ? '#1f2937' : '#ffffff', // gray-800 vs white
                color: dark ? '#f3f4f6' : '#1f2937',      // gray-100 vs gray-800
                confirmButtonColor: '#4f46e5',            // indigo-600
                cancelButtonColor: dark ? '#4b5563' : '#9ca3af', // gray-600 vs gray-400
            };
        };

        Livewire.on('swal:success', (data) => {
            Swal.fire({
                ...getSwalConfig(),
                title: 'Success!',
                text: data[0].message,
                icon: 'success',
            });
        });

        Livewire.on('swal:error', (data) => {
            Swal.fire({
                ...getSwalConfig(),
                title: 'Error!',
                text: data[0].message,
                icon: 'error',
                confirmButtonColor: '#ef4444' // Red override
            });
        });

        Livewire.on('swal:confirm', (eventData) => {
            const data = eventData[0]; 
            const config = getSwalConfig();

            Swal.fire({
                ...config,
                title: data.title,
                text: data.text,
                icon: data.icon || 'warning',
                
                showCancelButton: true,
                
                // Allow PHP to override text
                confirmButtonText: data.confirmButtonText || 'Yes, proceed!',
                cancelButtonText: data.cancelButtonText || 'Cancel',

                // Allow PHP to override colors
                confirmButtonColor: data.confirmButtonColor || config.confirmButtonColor,
                cancelButtonColor: data.cancelButtonColor || '#ef4444', 
            }).then((result) => {
                if (result.isConfirmed) {
                    Livewire.dispatch(data.method); 
                }
            });
        });
    });
</script>