#define NOB_STRIP_PREFIX
#include "../../nob.h"

#include "master_detail.h"
#include "src/db/db.h"
#include "core/layout/header.h"
#include "core/layout/footer.h"
#include "core/http/utils.h"

// static MD_Column room_type_columns[] = {
//     { .name = "type",        .label = "Type",        .type = COL_TYPE_TEXT,     .nullable = false },
//     { .name = "description", .label = "Description", .type = COL_TYPE_TEXTAREA, .nullable = true },
// };
//
// static MD_Column room_columns[] = {
//     {
//         .name = "room_type_id",
//         .label = "Room Type",
//         .type = COL_TYPE_FK_SELECT,
//         .nullable = false,
//         .fk_table = "RoomTypes",
//         .fk_label = "type",
//         .fk_value = "id"
//     },
//     { .name = "price",    .label = "Price",    .type = COL_TYPE_NUM,  .nullable = false },
//     { .name = "currency", .label = "Currency", .type = COL_TYPE_TEXT, .nullable = false },
// };
//
// static MD_ChildTab room_type_children[] = {
//     {
//         .table = "RoomTypes",
//         .title = "Room Types",
//         .fk_column = "room_type_id",
//         .id_column = "id",
//         .crud_path = "/room-types",
//         .columns = room_columns,
//         .column_count = ARRAY_LEN(room_columns)
//     },
// };

static MD_Column patient_columns[] = {
    { .name = "name",          .label = "Name",     .type = COL_TYPE_TEXT, .nullable = false },
    { .name = "sex",           .label = "Sex",      .type = COL_TYPE_TEXT, .nullable = true  },
    { .name = "age",           .label = "Age",      .type = COL_TYPE_NUM,  .nullable = true  },
    { .name = "tel",           .label = "Tel",      .type = COL_TYPE_TEXT, .nullable = true  },
    { .name = "patient_type",  .label = "Type",     .type = COL_TYPE_TEXT, .nullable = true  },
    { .name = "register_date", .label = "Register", .type = COL_TYPE_DATE, .nullable = true  },
    { .name = "district",      .label = "District", .type = COL_TYPE_TEXT, .nullable = true  },
    { .name = "province",      .label = "Province", .type = COL_TYPE_TEXT, .nullable = true  },
};

static MD_Column daily_invoice_columns[] = {
    { .name = "invoice_date",     .label = "Date",          .type = COL_TYPE_DATE, .nullable = true },
    { .name = "amount_in_riel",   .label = "Amount (Riel)", .type = COL_TYPE_NUM,  .nullable = true },
    { .name = "amount_in_dollar", .label = "Amount ($)",    .type = COL_TYPE_NUM,  .nullable = true },
};

static MD_Column medicine_invoice_columns[] = {
    {
        .name     = "patient_id",
        .label    = "Patient",
        .type     = COL_TYPE_FK_SELECT,
        .nullable = false,
        .fk_table = "Patients",
        .fk_label = "name",
        .fk_value = "id"
    },
    { .name = "invoice_date",     .label = "Date",          .type = COL_TYPE_DATE, .nullable = true },
    { .name = "amount_in_riel",   .label = "Amount (Riel)", .type = COL_TYPE_NUM,  .nullable = true },
    { .name = "amount_in_dollar", .label = "Amount ($)",    .type = COL_TYPE_NUM,  .nullable = true },
};

static MD_Column room_invoice_columns[] = {
    {
        .name = "patient_id",
        .label = "Patient",
        .type = COL_TYPE_FK_SELECT,
        .nullable = false,
        .fk_table = "Patients",
        .fk_label = "name",
        .fk_value = "id"
    },
    {
        .name = "room_id",
        .label = "Room",
        .type = COL_TYPE_FK_SELECT,
        .nullable = false,
        .fk_table = "Rooms",
        .fk_label = "name",
        .fk_value = "id"
    },
    { .name = "room_price", .label = "Room Price", .type = COL_TYPE_NUM,  .nullable = true },
    { .name = "start_date", .label = "Start Date", .type = COL_TYPE_DATE, .nullable = true },
    { .name = "end_date",   .label = "End Date",   .type = COL_TYPE_DATE, .nullable = true },
    { .name = "room_day",   .label = "Days",       .type = COL_TYPE_NUM,  .nullable = true },
};

static MD_Column appointment_columns[] = {
    {
        .name = "doctor_id",
        .label = "Doctor",
        .type = COL_TYPE_FK_SELECT,
        .nullable = false,
        .fk_table = "Doctors",
        .fk_label = "name",
        .fk_value = "id"
    },
    { .name = "date",         .label = "Date",        .type = COL_TYPE_DATE,      .nullable = true },
    { .name = "time",         .label = "Time",        .type = COL_TYPE_TEXT,      .nullable = true },
};

static MD_ChildTab patient_children[] = {
    {
        .table        = "PatientDailyInvoices",
        .title        = "Daily Invoices",
        .fk_column    = "patient_id",
        .id_column    = "id",
        .crud_path    = "/patient-daily-invoices",
        .columns      = daily_invoice_columns,
        .column_count = ARRAY_LEN(daily_invoice_columns)
    },
    {
        .table        = "PatientMedicineInvoices",
        .title        = "Medicine Invoices",
        .fk_column    = "patient_id",
        .id_column    = "id",
        .crud_path    = "/patient-medicine-invoices",
        .columns      = medicine_invoice_columns,
        .column_count = ARRAY_LEN(medicine_invoice_columns)
    },
    {
        .table        = "PatientInvoiceOut",
        .title        = "Room Invoices",
        .fk_column    = "patient_id",
        .id_column    = "id",
        .crud_path    = "/patient-invoice-out",
        .columns      = room_invoice_columns,
        .column_count = ARRAY_LEN(room_invoice_columns)
    },
    {
        .table        = "Appointment",
        .title        = "Appointments",
        .fk_column    = "patient_id",
        .id_column    = "id",
        .crud_path    = "/patients",
        .columns      = appointment_columns,
        .column_count = ARRAY_LEN(appointment_columns)
    },
};

static MD_Column pdi_detail_columns[] = {
    {
        .name     = "ill_type_id",
        .label    = "Illness",
        .type     = COL_TYPE_FK_SELECT,
        .nullable = false,
        .fk_table = "IllTypes",
        .fk_label = "type",
        .fk_value = "id"
    },
    { .name = "patientDaily_invoice_detail_price",    .label = "Price",    .type = COL_TYPE_NUM,  .nullable = true },
    { .name = "patientDaily_invoice_detail_currency", .label = "Currency", .type = COL_TYPE_TEXT, .nullable = true },
};
static MD_ChildTab pdi_children[] = {
    {
        .table        = "PatientDailyInvoicesDetails",
        .title        = "Details",
        .fk_column    = "patient_daily_invoice_id",
        .id_column    = "rowid",
        .crud_path    = "/patient-daily-invoices-details",
        .columns      = pdi_detail_columns,
        .column_count = ARRAY_LEN(pdi_detail_columns)
    },
};

static MD_Column pmi_detail_columns[] = {
    {
        .name     = "medicine_id",
        .label    = "Medicine",
        .type     = COL_TYPE_FK_SELECT,
        .nullable = false,
        .fk_table = "Medicines",
        .fk_label = "name",
        .fk_value = "id"
    },
    { .name = "qty",      .label = "Qty",      .type = COL_TYPE_NUM,  .nullable = true },
    { .name = "price",    .label = "Price",    .type = COL_TYPE_NUM,  .nullable = true },
    { .name = "amount",   .label = "Amount",   .type = COL_TYPE_NUM,  .nullable = true },
    { .name = "currency", .label = "Currency", .type = COL_TYPE_TEXT, .nullable = true },
};
static MD_ChildTab pmi_children[] = {
    {
        .table        = "PatientMedicineInvoiceDetails",
        .title        = "Details",
        .fk_column    = "patient_medicine_invoice_id",
        .id_column    = "rowid",
        .crud_path    = "/patient-medicine-invoices-details",
        .columns      = pmi_detail_columns,
        .column_count = ARRAY_LEN(pmi_detail_columns)
    },
};

static MD_Column pio_detail_columns[] = {
    {
        .name = "patient_daily_invoice_id",
        .label = "Daily Invoice",
        .type = COL_TYPE_FK_SELECT,
        .nullable = true,
        .fk_table = "PatientDailyInvoices",
        .fk_label = "id",
        .fk_value = "id"
    },
};
static MD_ChildTab pio_children[] = {
    {
        .table = "PatientInvoiceOutDetails",
        .title = "Details",
        .fk_column = "patient_invoice_out_id",
        .id_column = "rowid",
        .crud_path = "/patient-invoice-out-details",
        .columns = pio_detail_columns,
        .column_count = ARRAY_LEN(pio_detail_columns)
    },
};

static MD_Column org_columns[] = {
    { .name = "name",        .label = "Name",        .type = COL_TYPE_TEXT,     .nullable = false },
    { .name = "start_date",  .label = "Start Date",  .type = COL_TYPE_DATE,     .nullable = true  },
    { .name = "end_date",    .label = "End Date",    .type = COL_TYPE_DATE,     .nullable = true  },
    { .name = "description", .label = "Description", .type = COL_TYPE_TEXTAREA, .nullable = true  },
};

static MD_Column org_daily_invoice_columns[] = {
    {
        .name = "organization_id",
        .label = "Organization",
        .type = COL_TYPE_FK_SELECT,
        .nullable = false,
        .fk_table = "Organizations",
        .fk_label = "name",
        .fk_value = "id"
    },
    {
        .name = "patient_id",
        .label = "Patient",
        .type = COL_TYPE_FK_SELECT,
        .nullable = false,
        .fk_table = "Patients",
        .fk_label = "name",
        .fk_value = "id"
    },
    { .name = "invoice_date",     .label = "Date",          .type = COL_TYPE_DATE, .nullable = true },
    { .name = "amount_in_riel",   .label = "Amount (Riel)", .type = COL_TYPE_NUM,  .nullable = true },
    { .name = "amount_in_dollar", .label = "Amount ($)",    .type = COL_TYPE_NUM,  .nullable = true },
};

static MD_Column org_invoice_columns[] = {
    {
        .name = "organization_id",
        .label = "Organization",
        .type = COL_TYPE_FK_SELECT,
        .nullable = false,
        .fk_table = "Organizations",
        .fk_label = "name",
        .fk_value = "id"
    },
    { .name = "invoice_date",     .label = "Date",          .type = COL_TYPE_DATE, .nullable = true },
    { .name = "amount_in_riel",   .label = "Amount (Riel)", .type = COL_TYPE_NUM,  .nullable = true },
    { .name = "amount_in_dollar", .label = "Amount ($)",    .type = COL_TYPE_NUM,  .nullable = true },
};


static MD_Column org_payment_columns[] = {
    {
        .name = "organization_id",
        .label = "Organization",
        .type = COL_TYPE_FK_SELECT,
        .nullable = false,
        .fk_table = "Organizations",
        .fk_label = "name",
        .fk_value = "id"
    },
    {
        .name = "organization_invoice_id",
        .label = "Org Invoice",
        .type = COL_TYPE_FK_SELECT,
        .nullable = true,
        .fk_table = "OrganizationInvoices",
        .fk_label = "invoice_date",
        .fk_value = "id"
    },
    { .name = "date",                   .label = "Date",          .type = COL_TYPE_DATE,     .nullable = true },
    { .name = "price",                  .label = "Price",         .type = COL_TYPE_NUM,      .nullable = true },
    { .name = "currency",               .label = "Currency",      .type = COL_TYPE_TEXT,     .nullable = true },
};

static MD_Column org_balance_columns[] = {
    { .name = "balance_amount", .label = "Balance Amount", .type = COL_TYPE_NUM,  .nullable = true },
    { .name = "balance",        .label = "Balance",        .type = COL_TYPE_NUM,  .nullable = true },
};

static MD_Column org_invoice_detail_columns[] = {
    {
        .name = "medicine_id",
        .label = "Medicine",
        .type = COL_TYPE_FK_SELECT,
        .nullable = false,
        .fk_table = "Medicines",
        .fk_label = "name",
        .fk_value = "id"
    },
    { .name = "qty",         .label = "Qty",      .type = COL_TYPE_NUM,       .nullable = true },
    { .name = "price",       .label = "Price",    .type = COL_TYPE_NUM,       .nullable = true },
    { .name = "amount",      .label = "Amount",   .type = COL_TYPE_NUM,       .nullable = true },
    { .name = "currency",    .label = "Currency", .type = COL_TYPE_TEXT,      .nullable = true },
};
static MD_ChildTab org_inv_detail_children[] = {
    {
        .table = "OrganizationInvoiceDetail",
        .title = "Details",
        .fk_column = "organization_invoice_id",
        .id_column = "rowid",
        .crud_path = "/organization-invoice-details",
        .columns = org_invoice_detail_columns,
        .column_count = ARRAY_LEN(org_invoice_detail_columns)
    },
};

static MD_Column org_out_columns[] = {
    {
        .name = "organization_id",
        .label = "Organization",
        .type = COL_TYPE_FK_SELECT,
        .nullable = false,
        .fk_table = "Organizations",
        .fk_label = "name",
        .fk_value = "id"
    },
    {
        .name = "patient_id",
        .label = "Patient",
        .type = COL_TYPE_FK_SELECT,
        .nullable = false,
        .fk_table = "Patients",
        .fk_label = "name",
        .fk_value = "id"
    },
    {
        .name = "room_id",
        .label = "Room",
        .type = COL_TYPE_FK_SELECT,
        .nullable = true,
        .fk_table = "Rooms",
        .fk_label = "price",
        .fk_value = "id"
    },
    { .name = "room_price",   .label = "Room Price", .type = COL_TYPE_NUM,       .nullable = true },
    { .name = "start_date",   .label = "Start Date", .type = COL_TYPE_DATE,      .nullable = true },
    { .name = "end_date",     .label = "End Date",   .type = COL_TYPE_DATE,      .nullable = true },
    { .name = "room_day",     .label = "Days",       .type = COL_TYPE_NUM,       .nullable = true },
};

static MD_ChildTab org_children[] = {
    {
        .table        = "OrganizationDailyInvoices",
        .title        = "Daily Invoices",
        .fk_column    = "organization_id",
        .id_column    = "id",
        .crud_path    = "/organization-daily-invoices",
        .columns      = org_daily_invoice_columns,
        .column_count = ARRAY_LEN(org_daily_invoice_columns)
    },
    {
        .table        = "OrganizationInvoices",
        .title        = "Invoices",
        .fk_column    = "organization_id",
        .id_column    = "id",
        .crud_path    = "/organization-invoices",
        .columns      = org_invoice_columns,
        .column_count = ARRAY_LEN(org_invoice_columns)
    },
    {
        .table        = "OrganizationPayment",
        .title        = "Payments",
        .fk_column    = "organization_id",
        .id_column    = "id",
        .crud_path    = "/organization-payments",
        .columns      = org_payment_columns,
        .column_count = ARRAY_LEN(org_payment_columns)
    },
    {
        .table        = "OrganizationBalance",
        .title        = "Balances",
        .fk_column    = "organization_id",
        .id_column    = "rowid",
        .crud_path    = "/organization-balances",
        .columns      = org_balance_columns,
        .column_count = ARRAY_LEN(org_balance_columns)
    },
    {
        .table        = "OrganizationInvoiceOut",
        .title        = "Invoice Out",
        .fk_column    = "organization_id",
        .id_column    = "id",
        .crud_path    = "/organization-invoice-out",
        .columns      = org_out_columns,
        .column_count = ARRAY_LEN(org_out_columns)
    },
};

static MD_Column org_di_detail_columns[] = {
    {
        .name = "ill_id",
        .label = "Illness",
        .type = COL_TYPE_FK_SELECT,
        .nullable = false,
        .fk_table = "IllTypes",
        .fk_label = "type",
        .fk_value = "id"
    },
    { .name = "price",    .label = "Price",    .type = COL_TYPE_NUM,  .nullable = true },
    { .name = "currency", .label = "Currency", .type = COL_TYPE_TEXT, .nullable = true },
};
static MD_ChildTab org_di_children[] = {
    {
        .table = "OrganizationDailyInvoiceDetails",
        .title = "Details",
        .fk_column = "organization_daily_id",
        .id_column = "rowid",
        .crud_path = "/organization-daily-invoices-details",
        .columns = org_di_detail_columns,
        .column_count = ARRAY_LEN(org_di_detail_columns)
    },
};

static MD_Column org_io_detail_columns[] = {
    {
        .name = "organization_invoice_id",
        .label = "Org Invoice",
        .type = COL_TYPE_FK_SELECT,
        .nullable = true,
        .fk_table = "OrganizationInvoices",
        .fk_label = "invoice_date",
        .fk_value = "id"
    },
};
static MD_ChildTab org_io_children[] = {
    {
        .table = "OrganizationInvoiceOutDetails",
        .title = "Details",
        .fk_column = "organization_invoice_out_id",
        .id_column = "rowid",
        .crud_path = "/organization-invoice-out-details",
        .columns = org_io_detail_columns,
        .column_count = ARRAY_LEN(org_io_detail_columns)
    },
};

static MD_Column med_import_columns[] = {
    {
        .name = "medicine_id",
        .label = "Medicine",
        .type = COL_TYPE_FK_SELECT,
        .nullable = false,
        .fk_table = "Medicines",
        .fk_label = "name",
        .fk_value = "id"
    },
    { .name = "date",        .label = "Date",     .type = COL_TYPE_DATE,      .nullable = true },
    { .name = "qty",         .label = "Qty",      .type = COL_TYPE_NUM,       .nullable = false },
    { .name = "price",       .label = "Price",    .type = COL_TYPE_NUM,       .nullable = true  },
};

static MD_Column ill_price_columns[] = {
    { .name = "name",        .label = "Name",        .type = COL_TYPE_TEXT,     .nullable = false },
    { .name = "price",       .label = "Price",       .type = COL_TYPE_NUM,      .nullable = true  },
    { .name = "currency",    .label = "Currency",    .type = COL_TYPE_TEXT,     .nullable = true  },
    { .name = "description", .label = "Description", .type = COL_TYPE_TEXTAREA, .nullable = true  },
};
static MD_Column ill_type_columns[] = {
    { .name = "type",        .label = "Type",        .type = COL_TYPE_TEXT,     .nullable = false },
    { .name = "description", .label = "Description", .type = COL_TYPE_TEXTAREA, .nullable = true  },
};
static MD_Column department_columns[] = {
    { .name = "name",        .label = "Name",        .type = COL_TYPE_TEXT,     .nullable = false },
    { .name = "description", .label = "Description", .type = COL_TYPE_TEXTAREA, .nullable = true  },
};
static MD_Column doctor_columns[] = {
    { .name = "name",  .label = "Name",  .type = COL_TYPE_TEXT, .nullable = false },
    { .name = "sex",   .label = "Sex",   .type = COL_TYPE_TEXT, .nullable = true  },
    { .name = "skill", .label = "Skill", .type = COL_TYPE_TEXT, .nullable = true  },
    { .name = "tel",   .label = "Tel",   .type = COL_TYPE_TEXT, .nullable = true  },
    { .name = "addr",  .label = "Addr",  .type = COL_TYPE_TEXT, .nullable = true  },
};
static MD_Column medicine_type_columns[] = {
    { .name = "type",        .label = "Type",        .type = COL_TYPE_TEXT,     .nullable = false },
    { .name = "description", .label = "Description", .type = COL_TYPE_TEXTAREA, .nullable = true  },
};
static MD_Column medicines_columns[] = {
    {
        .name = "supplier_id",
        .label = "Supplier",
        .type = COL_TYPE_FK_SELECT,
        .nullable = false,
        .fk_table = "Supplier",
        .fk_label = "company",
        .fk_value = "id"
    },
    { .name = "name",         .label = "Name",      .type = COL_TYPE_TEXT,      .nullable = false },
    { .name = "price",        .label = "Price",     .type = COL_TYPE_NUM,       .nullable = false },
    { .name = "currency",     .label = "Currency",  .type = COL_TYPE_TEXT,      .nullable = true  },
    { .name = "percent",      .label = "Percent",   .type = COL_TYPE_NUM,       .nullable = true  },
    { .name = "description",  .label = "Description", .type = COL_TYPE_TEXTAREA, .nullable = true },
};
static MD_ChildTab ill_type_children[] = {
    {
        .table = "IllPrices",
        .title = "Prices",
        .fk_column = "ill_type_id",
        .id_column = "id",
        .crud_path = "/ill-prices",
        .columns = ill_price_columns,
        .column_count = ARRAY_LEN(ill_price_columns)
    },
};


/* Doctors page: pick department + time */
static MD_Column wt_for_doctor_columns[] = {
    {
        .name = "department_id",
        .label = "Department",
        .type = COL_TYPE_FK_SELECT,
        .nullable = true,
        .fk_table = "Departments",
        .fk_label = "name",
        .fk_value = "id"
    },
    { .name = "time", .label = "Time", .type = COL_TYPE_TEXT, .nullable = false },
};
static MD_ChildTab doctor_children[] = {
    {
        .table = "DoctorWorkTimes",
        .title = "Work Times",
        .fk_column = "doctor_id",
        .id_column = "rowid",
        .crud_path = "/doctor-work-times",
        .columns = wt_for_doctor_columns,
        .column_count = ARRAY_LEN(wt_for_doctor_columns)
    },
};

/* Departments page: pick doctor + time */
static MD_Column wt_for_dept_columns[] = {
    {
        .name = "doctor_id",
        .label = "Doctor",
        .type = COL_TYPE_FK_SELECT,
        .nullable = false,
        .fk_table = "Doctors",
        .fk_label = "name",
        .fk_value = "id"
    },
    { .name = "time", .label = "Time", .type = COL_TYPE_TEXT, .nullable = false },
};

static MD_ChildTab dept_children[] = {
    {
        .table = "DoctorWorkTimes",
        .title = "Work Times",
        .fk_column = "department_id",
        .id_column = "rowid",
        .crud_path = "/doctor-work-times",
        .columns = wt_for_dept_columns,
        .column_count = ARRAY_LEN(wt_for_dept_columns)
    },
};

static MD_ChildTab med_type_children[] = {
    {
        .table = "Medicines",
        .title = "Medicines",
        .fk_column = "medicine_type_id",
        .id_column = "id",
        .crud_path = "/medicines",
        .columns = medicines_columns,
        .column_count = ARRAY_LEN(medicines_columns)
    },
};

static MD_Column med_import_detail_columns[] = {
    {
        .name = "medicine_id",
        .label = "Medicine",
        .type = COL_TYPE_FK_SELECT,
        .nullable = false,
        .fk_table = "Medicines",
        .fk_label = "name",
        .fk_value = "id"
    },
    {
        .name = "qty",
        .label = "Qty",
        .type = COL_TYPE_NUM,
        .nullable = true
    },
    {
        .name = "price",
        .label = "Price",
        .type = COL_TYPE_NUM,
        .nullable = true
    },
};

static MD_ChildTab med_import_children[] = {
    {
        .table = "MedicineImportDetails",
        .title = "Import Details",
        .fk_column = "medicine_import_id",
        .id_column = "id",
        .crud_path = "/medicine-imports",
        .columns = med_import_detail_columns,
        .column_count = ARRAY_LEN(med_import_detail_columns)
    },
};

static MD_MasterConfig master_configs[] = {
    // {
    //     .table          = "RoomTypes",
    //     .title          = "Room Types",
    //     .id_column      = "id",
    //     .crud_path      = "/room-types",
    //     .columns        = room_type_columns,
    //     .column_count   = ARRAY_LEN(room_type_columns),
    //     .children       = room_type_children,
    //     .children_count = ARRAY_LEN(room_type_children)
    // },
    {
        .table          = "Patients",
        .title          = "Patients",
        .id_column      = "id",
        .crud_path      = "/patients",
        .columns        = patient_columns,
        .column_count   = ARRAY_LEN(patient_columns),
        .children       = patient_children,
        .children_count = ARRAY_LEN(patient_children)
    },
    {
        .table          = "Organizations",
        .title          = "Organizations",
        .id_column      = "id",
        .crud_path      = "/organizations",
        .columns        = org_columns,
        .column_count   = ARRAY_LEN(org_columns),
        .children       = org_children,
        .children_count = ARRAY_LEN(org_children)
    },
    {
        .table          = "MedicineImport",
        .title          = "Medicine Import",
        .id_column      = "id",
        .crud_path      = "/medicine-imports",
        .columns        = med_import_columns,
        .column_count   = ARRAY_LEN(med_import_columns),
        .children       = med_import_children,
        .children_count = ARRAY_LEN(med_import_children)
    },
    {
        .table          = "PatientDailyInvoices",
        .title          = "Patient Daily Invoices",
        .id_column      = "id",
        .crud_path      = "/patient-daily-invoices",
        .columns        = daily_invoice_columns,
        .column_count   = ARRAY_LEN(daily_invoice_columns),
        .children       = pdi_children,
        .children_count = ARRAY_LEN(pdi_children)
    },
    {
        .table          = "PatientMedicineInvoices",
        .title          = "Patient Medicine Invoices",
        .id_column      = "id",
        .crud_path      = "/patient-medicine-invoices",
        .columns        = medicine_invoice_columns,
        .column_count   = ARRAY_LEN(medicine_invoice_columns),
        .children       = pmi_children,
        .children_count = ARRAY_LEN(pmi_children)
    },
    {
        .table          = "PatientInvoiceOut",
        .title          = "Patient Room Invoices",
        .id_column      = "id",
        .crud_path      = "/patient-invoice-out",
        .columns        = room_invoice_columns,
        .column_count   = ARRAY_LEN(room_invoice_columns),
        .children       = pio_children,
        .children_count = ARRAY_LEN(pio_children)
    },
    {
        .table          = "OrganizationDailyInvoices",
        .title          = "Patient Medicine Invoices",
        .id_column      = "id",
        .crud_path      = "/organization-daily-invoices",
        .columns        = org_daily_invoice_columns,
        .column_count   = ARRAY_LEN(org_daily_invoice_columns),
        .children       = org_di_children,
        .children_count = ARRAY_LEN(org_di_children)
    },
    {
        .table          = "OrganizationInvoiceOut",
        .title          = "Org Invoice Out",
        .id_column      = "id",
        .crud_path      = "/organization-invoice-out",
        .columns        = org_out_columns,
        .column_count   = ARRAY_LEN(org_out_columns),
        .children       = org_io_children,
        .children_count = ARRAY_LEN(org_io_children)
    },
    {
        .table          = "IllTypes",
        .title          = "Illness & Price",
        .id_column      = "id",
        .crud_path      = "/ill-types",
        .columns        = ill_type_columns,
        .column_count   = ARRAY_LEN(ill_type_columns),
        .children       = ill_type_children,
        .children_count = ARRAY_LEN(ill_type_children)
    },
    {
        .table          = "Departments",
        .title          = "Departments",
        .id_column      = "id",
        .crud_path      = "/departments",
        .columns        = department_columns,
        .column_count   = ARRAY_LEN(department_columns),
        .children       = dept_children,
        .children_count = ARRAY_LEN(dept_children)
    },
    {
        .table          = "Doctors",
        .title          = "Doctors",
        .id_column      = "id",
        .crud_path      = "/doctors",
        .columns        = doctor_columns,
        .column_count   = ARRAY_LEN(doctor_columns),
        .children       = doctor_children,
        .children_count = ARRAY_LEN(doctor_children)
    },
    {
        .table          = "MedicineTypes",
        .title          = "Medicine Categories",
        .id_column      = "id",
        .crud_path      = "/medicine-types",
        .columns        = medicine_type_columns,
        .column_count   = ARRAY_LEN(medicine_type_columns),
        .children       = med_type_children,
        .children_count = ARRAY_LEN(med_type_children)
    },
    {
        .table          = "OrganizationInvoices",
        .title          = "Org Invoices",
        .id_column      = "id",
        .crud_path      = "/organization-invoices",
        .columns        = org_invoice_columns,
        .column_count   = ARRAY_LEN(org_invoice_columns),
        .children       = org_inv_detail_children,
        .children_count = ARRAY_LEN(org_inv_detail_children)
    },
};

static size_t master_configs_count = ARRAY_LEN(master_configs);

const MD_MasterConfig *find_master_config(const char *table) {
    for (size_t i = 0; i < master_configs_count; ++i) {
        if (strcmp(master_configs[i].table, table) == 0) {
            return &master_configs[i];
        }
    }
    return NULL;
}

MD_MasterRows *md_master_rows_new(void) {
    MD_MasterRows *rows = malloc(sizeof(MD_MasterRows));
    rows->items = NULL;
    rows->count = 0;
    rows->capacity = 0;
    return rows;
}

void md_master_rows_free(MD_MasterRows *rows) {
    if (!rows) return;
    for (size_t i = 0; i < rows->count; ++i) {
        MD_MasterRow *row = &rows->items[i];
        free(row->values);
        free(row->disp);
        if (row->children) {
            for (size_t ci = 0; ci < row->children_count; ++ci) {
                MD_ChildRows *crows = &row->children[ci];
                free(crows->items);
            }
            free(row->children);
        }
    }
    free(rows->items);
    free(rows);
}

static char *col_to_str(sqlite3_stmt *stmt, int col) {
    const char *val = (const char *)sqlite3_column_text(stmt, col);
    return val ? temp_strdup(val) : temp_strdup("");
}

static char *build_column_list(const MD_ChildTab *child) {
    String_Builder sb = {0};
    sb_append_cstr(&sb, child->id_column);
    for (size_t i = 0; i < child->column_count; ++i) {
        sb_append_cstr(&sb, ", ");
        sb_append_cstr(&sb, child->columns[i].name);
    }
    sb_append_null(&sb);
    return sb.items;
}

static bool load_child_rows(sqlite3           *db,
                            const MD_ChildTab *child,
                            long long          master_id,
                            MD_ChildRows      *out_rows)
{
    char *cols = build_column_list(child);
    char *sql = temp_sprintf("SELECT %s FROM %s WHERE %s = %lld ORDER BY %s DESC;",
            cols, child->table, child->fk_column, master_id, child->id_column);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return false;
    }
    int ret;
    for (ret = sqlite3_step(stmt); ret == SQLITE_ROW; ret = sqlite3_step(stmt)) {
        int col_count = sqlite3_column_count(stmt);
        MD_ChildRow row = {0};
        row.value_count = col_count;
        row.values = malloc(col_count * sizeof(char *));
        row.disp   = malloc(col_count * sizeof(char *));
        for (int i = 0; i < col_count; ++i) {
            row.values[i] = col_to_str(stmt, i);
            row.disp[i]   = temp_strdup(row.values[i]);
        }
        da_append(out_rows, row);
    }
    if (ret != SQLITE_DONE) {
        LOG_SQLITE3_ERROR(db);
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);
    return true;
}


static void md_col_load_options(sqlite3 *db, MD_Column *col) {
    if (!col || col->type != COL_TYPE_FK_SELECT || !col->fk_table) return;
    char *sql = temp_sprintf("SELECT id, %s FROM %s ORDER BY %s ASC;",
            col->fk_label, col->fk_table, col->fk_label);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return;
    }
    MD_Option *items = NULL;
    size_t count = 0, cap = 0;
    int ret;
    for (ret = sqlite3_step(stmt); ret == SQLITE_ROW; ret = sqlite3_step(stmt)) {
        if (count == cap) {
            cap = cap ? cap * 2 : 8;
            items = realloc(items, cap * sizeof(MD_Option));
        }
        items[count].id = sqlite3_column_int64(stmt, 0);
        const char *l = (const char *)sqlite3_column_text(stmt, 1);
        items[count].label = temp_strdup(l ? l : "");
        count++;
    }
    sqlite3_finalize(stmt);
    col->opt = items;
    col->opt_count = count;
}

static const char *md_fk_display(const MD_Column *col, const char *id) {
    if (!col || col->type != COL_TYPE_FK_SELECT || !id || !id[0]) return id;
    long long v = atoll(id);
    for (size_t i = 0; i < col->opt_count; ++i) {
        if (col->opt[i].id == v) return col->opt[i].label;
    }
    return id;
}

bool md_form_cols_load(sqlite3 *db, const MD_MasterConfig *config, MD_FormCols *out) {

    for (size_t i = 0; i < config->column_count; ++i) {
        MD_FormCol fc = { .col = &config->columns[i] };
        if (fc.col->type == COL_TYPE_FK_SELECT && fc.col->fk_table) {
            char *sql = temp_sprintf("SELECT id, %s FROM %s ORDER BY %s ASC;",
                    fc.col->fk_label, fc.col->fk_table, fc.col->fk_label);
            sqlite3_stmt *stmt = NULL;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
                LOG_SQLITE3_ERROR(db);
                return false;
            }
            int ret;
            for (ret = sqlite3_step(stmt); ret == SQLITE_ROW; ret = sqlite3_step(stmt)) {
                MD_Option o = {0};
                o.id = sqlite3_column_int64(stmt, 0);
                const char *l = (const char *)sqlite3_column_text(stmt, 1);
                o.label = temp_strdup(l ? l : "");
                da_append(&fc, o);
            }
            sqlite3_finalize(stmt);
        }
        da_append(out, fc);
    }
    return true;
}

bool md_load_master_with_children(sqlite3               *db,
                                  const MD_MasterConfig *config,
                                  MD_MasterRows         *rows)
{
    String_Builder cl = {0};
    sb_append_cstr(&cl, config->id_column);
    for (size_t ci2 = 0; ci2 < config->column_count; ++ci2) {
        sb_append_cstr(&cl, ", ");
        sb_append_cstr(&cl, config->columns[ci2].name);
    }
    sb_append_null(&cl);
    char *sql = temp_sprintf("SELECT %s FROM %s ORDER BY %s DESC;", cl.items, config->table, config->id_column);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return false;
    }
    int ret;
    for (ret = sqlite3_step(stmt); ret == SQLITE_ROW; ret = sqlite3_step(stmt)) {
        int col_count = sqlite3_column_count(stmt);
        MD_MasterRow row = {0};
        row.id = sqlite3_column_int64(stmt, 0);
        row.value_count = col_count;
        row.values = malloc(col_count * sizeof(char *));
        row.disp   = malloc(col_count * sizeof(char *));
        for (int i = 0; i < col_count; ++i) {
            row.values[i] = col_to_str(stmt, i);
            row.disp[i]   = temp_strdup(row.values[i]);
        }
        row.children = malloc(config->children_count * sizeof(MD_ChildRows));
        row.children_count = config->children_count;
        for (size_t ci = 0; ci < config->children_count; ++ci) {
            row.children[ci].items = NULL;
            row.children[ci].count = 0;
            row.children[ci].capacity = 0;
            if (!load_child_rows(db, &config->children[ci], row.id, &row.children[ci])) {
                free(row.values);
                for (size_t ci2 = 0; ci2 <= ci; ++ci2) {
                    MD_ChildRows *cr = &row.children[ci2];
                    for (size_t j = 0; j < cr->count; ++j) {
                        free(cr->items[j].values);
                    }
                    free(cr->items);
                }
                free(row.children);
                sqlite3_finalize(stmt);
                return false;
            }
        }
        da_append(rows, row);
    }
    if (ret != SQLITE_DONE) {
        LOG_SQLITE3_ERROR(db);
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);
    return true;
}

void serve_master_detail_list(Serve_Context *sc, const MD_MasterConfig *config) {
    sqlite3 *db = open_webc_db();
    if (!db) {
        serve_error(sc, 500);
        return;
    }
    MD_MasterRows *rows = md_master_rows_new();
    MD_FormCols form_cols = {0};
    bool ok = md_load_master_with_children(db, config, rows)
        && md_form_cols_load(db, config, &form_cols);
    if (ok) {
        for (size_t i = 0; i < config->column_count; ++i) {
            md_col_load_options(db, &config->columns[i]);
        }
        for (size_t ti = 0; ti < config->children_count; ++ti) {
            for (size_t cci = 0; cci < config->children[ti].column_count; ++cci) {
                md_col_load_options(db, &config->children[ti].columns[cci]);
            }
        }
        // FK label substitution into display arrays
        for (size_t ri = 0; ri < rows->count; ++ri) {
            MD_MasterRow *r = &rows->items[ri];
            for (size_t ci = 0; ci < config->column_count; ++ci) {
                r->disp[ci + 1] = (char *)md_fk_display(&config->columns[ci], r->values[ci + 1]);
            }
            for (size_t ti = 0; ti < config->children_count; ++ti) {
                const MD_ChildTab *ct = &config->children[ti];
                MD_ChildRows *cr = &r->children[ti];
                for (size_t cj = 0; cj < cr->count; ++cj) {
                    for (size_t cci = 0; cci < ct->column_count; ++cci) {
                        cr->items[cj].disp[cci + 1] = (char *)md_fk_display(&ct->columns[cci], cr->items[cj].values[cci + 1]);
                    }
                }
            }
        }
    }
    sqlite3_close(db);
    if (!ok) {
        md_master_rows_free(rows);
        serve_error(sc, 500);
        return;
    }
    String_Builder *sb = &sc->body;
    sb->count = 0;
    render_page_header(sb, config->title, config->table);
#define OUT(buf, size) sb_append_buf(sb, buf, size);
#define INT(v) sb_append_cstr(sb, temp_sprintf("%zu", v));
#define LLINT(v) sb_append_cstr(sb, temp_sprintf("%lld", v));
#define STR(s) sb_append_cstr(sb, s);
#define ESCAPED(s) sb_append_html_escaped(sb, s ? s : "");
#define PAGE_TITLE(s) sb_append_cstr(sb, s);
#include "build/h_to_html/component/master_detail.h"
#undef OUT
#undef INT
#undef LLINT
#undef STR
#undef ESCAPED
#undef PAGE_TITLE
    render_page_footer(sb);
    md_master_rows_free(rows);
    http_render_response(sc, 200, "text/html", sb_to_sv(*sb));
}

void serve_master_detail_by_table(Serve_Context *sc, const char *master_table) {
    const MD_MasterConfig *config = find_master_config(master_table);
    if (!config) {
        serve_error(sc, 404);
        return;
    }
    serve_master_detail_list(sc, config);
}
